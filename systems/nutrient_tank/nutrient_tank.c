#include "nutrient_tank.h"
#include <string.h>

/* Use a finite slice for "continuous" circulation to simplify logic.
   PumpUnit internal max_run_time_ms still applies. */
static const uint32_t k_circulation_slice_ms = 60000U;

/* Wrap-safe time compare (valid if deadlines are within +/- 2^31 ms). */
static uint8_t nt_time_reached(uint32_t now_ms, uint32_t target_ms)
{
    return (((int32_t)(now_ms - target_ms)) >= 0) ? 1U : 0U;
}

static void nt_push_event(NutrientTank_t *tank,
                          NutrientTank_EventType_t type,
                          uint32_t main_ul,
                          uint32_t ret_ul,
                          NutrientTank_Error_t err,
                          PumpGuard_BlockReason_t block_reason)
{
    uint8_t next_wr;

    if (tank == NULL || tank->events == NULL || tank->cfg.event_queue_size == 0U) {
        return;
    }

    next_wr = (uint8_t)(tank->st.ev_wr + 1U);
    if (next_wr >= tank->cfg.event_queue_size) {
        next_wr = 0U;
    }

    /* If buffer is full, drop the oldest event (advance read index). */
    if (next_wr == tank->st.ev_rd) {
        uint8_t next_rd = (uint8_t)(tank->st.ev_rd + 1U);
        if (next_rd >= tank->cfg.event_queue_size) {
            next_rd = 0U;
        }
        tank->st.ev_rd = next_rd;
    }

    tank->events[tank->st.ev_wr].type = type;
    tank->events[tank->st.ev_wr].main_volume_ul = main_ul;
    tank->events[tank->st.ev_wr].return_volume_ul = ret_ul;
    tank->events[tank->st.ev_wr].error = err;
    tank->events[tank->st.ev_wr].block_reason = block_reason;

    tank->st.ev_wr = next_wr;
}

static void nt_stop_guard(PumpGuard_t *g)
{
    if (g == NULL) {
        return;
    }
    (void)pump_guard_stop(g);
}

static void nt_stop_all_pumps(NutrientTank_t *tank)
{
    uint8_t i;

    if (tank == NULL) {
        return;
    }

    nt_stop_guard(tank->cfg.water_in);

    for (i = 0U; i < tank->cfg.nutrient_count && i < NUTRIENT_TANK_NUTRIENT_MAX_PUMPS; i++) {
        nt_stop_guard(tank->cfg.nutrients[i]);
    }

    nt_stop_guard(tank->cfg.ph_up);
    nt_stop_guard(tank->cfg.ph_down);

    nt_stop_guard(tank->cfg.air);
    nt_stop_guard(tank->cfg.circulation);

    nt_stop_guard(tank->cfg.drain);
    nt_stop_guard(tank->cfg.return_pump);
}

static void nt_process_all_guards(NutrientTank_t *tank, uint32_t now_ms)
{
    uint8_t i;

    if (tank == NULL) {
        return;
    }

    if (tank->cfg.water_in != NULL) {
        pump_guard_process(tank->cfg.water_in, now_ms);
    }

    for (i = 0U; i < tank->cfg.nutrient_count && i < NUTRIENT_TANK_NUTRIENT_MAX_PUMPS; i++) {
        if (tank->cfg.nutrients[i] != NULL) {
            pump_guard_process(tank->cfg.nutrients[i], now_ms);
        }
    }

    if (tank->cfg.ph_up != NULL) {
        pump_guard_process(tank->cfg.ph_up, now_ms);
    }
    if (tank->cfg.ph_down != NULL) {
        pump_guard_process(tank->cfg.ph_down, now_ms);
    }
    if (tank->cfg.air != NULL) {
        pump_guard_process(tank->cfg.air, now_ms);
    }
    if (tank->cfg.circulation != NULL) {
        pump_guard_process(tank->cfg.circulation, now_ms);
    }
    if (tank->cfg.drain != NULL) {
        pump_guard_process(tank->cfg.drain, now_ms);
    }
    if (tank->cfg.return_pump != NULL) {
        pump_guard_process(tank->cfg.return_pump, now_ms);
    }
}

static uint8_t nt_level_is_available(const NutrientTank_Level_t *lvl,
                                    uint32_t now_ms,
                                    uint8_t *is_fault,
                                    uint8_t *is_stale)
{
    uint32_t age_ms;

    if (is_fault != NULL) {
        *is_fault = 0U;
    }
    if (is_stale != NULL) {
        *is_stale = 0U;
    }

    if (lvl == NULL || lvl->map_fn == NULL) {
        return 0U;
    }

    if (lvl->fault != 0U || lvl->valid == 0U) {
        if (is_fault != NULL) {
            *is_fault = 1U;
        }
        return 0U;
    }

    if (lvl->stale_timeout_ms != 0U) {
        age_ms = (uint32_t)(now_ms - lvl->last_update_ms);
        if (age_ms > lvl->stale_timeout_ms) {
            if (is_stale != NULL) {
                *is_stale = 1U;
            }
            return 0U;
        }
    }

    return 1U;
}

static NutrientTank_LevelState_t nt_eval_main_level_state(const NutrientTank_t *tank,
                                                         NutrientTank_LevelState_t prev,
                                                         uint32_t volume_ul)
{
    const NutrientTank_LevelPolicy_t *p;

    if (tank == NULL) {
        return prev;
    }

    p = &tank->cfg.level_policy;

    /* Critical always wins */
    if (p->main_critical_ul != 0U && volume_ul <= p->main_critical_ul) {
        return NUTRIENT_TANK_LEVEL_CRITICAL;
    }

    /* Hysteresis: LOW/CRITICAL -> keep LOW until resume threshold */
    if ((prev == NUTRIENT_TANK_LEVEL_LOW || prev == NUTRIENT_TANK_LEVEL_CRITICAL) &&
        p->main_resume_ul != 0U &&
        volume_ul < p->main_resume_ul) {
        return NUTRIENT_TANK_LEVEL_LOW;
    }

    if (p->main_low_ul != 0U && volume_ul <= p->main_low_ul) {
        return NUTRIENT_TANK_LEVEL_LOW;
    }

    if (p->main_high_ul != 0U && volume_ul >= p->main_high_ul) {
        return NUTRIENT_TANK_LEVEL_HIGH;
    }

    return NUTRIENT_TANK_LEVEL_OK;
}

static NutrientTank_LevelState_t nt_eval_return_level_state(const NutrientTank_t *tank,
                                                           NutrientTank_LevelState_t prev,
                                                           uint32_t volume_ul)
{
    const NutrientTank_LevelPolicy_t *p;

    if (tank == NULL) {
        return prev;
    }

    p = &tank->cfg.level_policy;

    /* Hysteresis: HIGH remains HIGH until below return_resume_ul (if set) */
    if (prev == NUTRIENT_TANK_LEVEL_HIGH && p->return_resume_ul != 0U && volume_ul > p->return_resume_ul) {
        return NUTRIENT_TANK_LEVEL_HIGH;
    }

    if (p->return_request_ul != 0U && volume_ul >= p->return_request_ul) {
        return NUTRIENT_TANK_LEVEL_HIGH;
    }

    return NUTRIENT_TANK_LEVEL_OK;
}

static uint8_t nt_main_allows_circulation(const NutrientTank_t *tank, uint32_t now_ms)
{
    uint8_t fault;
    uint8_t stale;

    if (tank == NULL) {
        return 0U;
    }

    /* If no main level sensor configured in tank, do not block here. */
    if (tank->cfg.main_level.map_fn == NULL) {
        return 1U;
    }

    (void)nt_level_is_available(&tank->cfg.main_level, now_ms, &fault, &stale);

    /* If main level is not available, block to be safe. */
    if (fault != 0U || stale != 0U) {
        return 0U;
    }

    if (tank->st.main_level_state == NUTRIENT_TANK_LEVEL_LOW ||
        tank->st.main_level_state == NUTRIENT_TANK_LEVEL_CRITICAL) {
        return 0U;
    }

    return 1U;
}

static uint8_t nt_main_allows_drain(const NutrientTank_t *tank, uint32_t now_ms)
{
    /* Same policy as circulation: never drain when low/critical or unknown. */
    return nt_main_allows_circulation(tank, now_ms);
}

static uint8_t nt_main_allows_addition(const NutrientTank_t *tank, uint32_t now_ms)
{
    uint8_t fault;
    uint8_t stale;

    if (tank == NULL) {
        return 0U;
    }

    /* If no main level sensor configured, do not block additions here. */
    if (tank->cfg.main_level.map_fn == NULL) {
        return 1U;
    }

    if (nt_level_is_available(&tank->cfg.main_level, now_ms, &fault, &stale) == 0U) {
        return 0U;
    }

    if (tank->cfg.level_policy.main_high_ul != 0U &&
        tank->cfg.main_level.last_volume_ul >= tank->cfg.level_policy.main_high_ul) {
        return 0U;
    }

    return 1U;
}

static uint8_t nt_main_allows_return(const NutrientTank_t *tank, uint32_t now_ms)
{
    uint8_t fault;
    uint8_t stale;

    if (tank == NULL) {
        return 0U;
    }

    if (tank->cfg.main_level.map_fn == NULL) {
        return 1U;
    }

    if (nt_level_is_available(&tank->cfg.main_level, now_ms, &fault, &stale) == 0U) {
        return 0U;
    }

    if (tank->cfg.level_policy.main_block_return_ul != 0U &&
        tank->cfg.main_level.last_volume_ul >= tank->cfg.level_policy.main_block_return_ul) {
        return 0U;
    }

    return 1U;
}

static PumpGuard_t *nt_get_guard_for_dose(NutrientTank_t *tank, const NutrientTank_Command_t *cmd)
{
    if (tank == NULL || cmd == NULL) {
        return NULL;
    }

    if (cmd->type != NUTRIENT_TANK_CMD_DOSE_VOLUME) {
        return NULL;
    }

    switch (cmd->p.dose.kind) {
        case NUTRIENT_TANK_DOSE_WATER:
            return tank->cfg.water_in;

        case NUTRIENT_TANK_DOSE_NUTRIENT:
            if (cmd->p.dose.nutrient_index < tank->cfg.nutrient_count &&
                cmd->p.dose.nutrient_index < NUTRIENT_TANK_NUTRIENT_MAX_PUMPS) {
                return tank->cfg.nutrients[cmd->p.dose.nutrient_index];
            }
            return NULL;

        case NUTRIENT_TANK_DOSE_PH_UP:
            return tank->cfg.ph_up;

        case NUTRIENT_TANK_DOSE_PH_DOWN:
            return tank->cfg.ph_down;

        case NUTRIENT_TANK_DOSE_DRAIN:
            return tank->cfg.drain;

        case NUTRIENT_TANK_DOSE_RETURN:
            return tank->cfg.return_pump;

        default:
            return NULL;
    }
}

static uint8_t nt_cmd_requires_after_dose_mix(const NutrientTank_Command_t *cmd)
{
    if (cmd == NULL || cmd->type != NUTRIENT_TANK_CMD_DOSE_VOLUME) {
        return 0U;
    }

    /* Mix after anything that adds into main tank. */
    switch (cmd->p.dose.kind) {
        case NUTRIENT_TANK_DOSE_WATER:
        case NUTRIENT_TANK_DOSE_NUTRIENT:
        case NUTRIENT_TANK_DOSE_PH_UP:
        case NUTRIENT_TANK_DOSE_PH_DOWN:
        case NUTRIENT_TANK_DOSE_RETURN:
            return 1U;

        default:
            return 0U;
    }
}

static void nt_update_level_states_and_events(NutrientTank_t *tank, uint32_t now_ms)
{
    uint8_t main_fault;
    uint8_t main_stale;
    uint8_t ret_fault;
    uint8_t ret_stale;

    uint8_t main_avail;
    uint8_t ret_avail;

    uint32_t main_ul = 0U;
    uint32_t ret_ul = 0U;

    NutrientTank_LevelState_t prev_main;
    NutrientTank_LevelState_t prev_ret;

    uint8_t req_return;
    uint8_t req_refill;

    if (tank == NULL) {
        return;
    }

    prev_main = tank->st.main_level_state;
    prev_ret = tank->st.return_level_state;

    main_avail = nt_level_is_available(&tank->cfg.main_level, now_ms, &main_fault, &main_stale);
    ret_avail = nt_level_is_available(&tank->cfg.return_level, now_ms, &ret_fault, &ret_stale);

    if (main_avail != 0U) {
        main_ul = tank->cfg.main_level.last_volume_ul;
        tank->st.main_level_state = nt_eval_main_level_state(tank, tank->st.main_level_state, main_ul);
    } else if (tank->cfg.main_level.map_fn != NULL) {
        /* Main sensor configured but not available -> treat as critical for safety decisions. */
        tank->st.main_level_state = NUTRIENT_TANK_LEVEL_CRITICAL;
    }

    if (ret_avail != 0U) {
        ret_ul = tank->cfg.return_level.last_volume_ul;
        tank->st.return_level_state = nt_eval_return_level_state(tank, tank->st.return_level_state, ret_ul);
    }

    /* Main level transition events */
    if (tank->st.main_level_state != prev_main) {
        if (tank->st.main_level_state == NUTRIENT_TANK_LEVEL_LOW) {
            nt_push_event(tank, NUTRIENT_TANK_EVENT_MAIN_LOW, main_ul, ret_ul, NUTRIENT_TANK_ERR_NONE, PUMP_GUARD_BLOCK_NONE);
        } else if (tank->st.main_level_state == NUTRIENT_TANK_LEVEL_CRITICAL) {
            nt_push_event(tank, NUTRIENT_TANK_EVENT_MAIN_CRITICAL, main_ul, ret_ul, NUTRIENT_TANK_ERR_NONE, PUMP_GUARD_BLOCK_NONE);
        } else {
            if (prev_main == NUTRIENT_TANK_LEVEL_LOW || prev_main == NUTRIENT_TANK_LEVEL_CRITICAL) {
                nt_push_event(tank, NUTRIENT_TANK_EVENT_MAIN_RESUMED, main_ul, ret_ul, NUTRIENT_TANK_ERR_NONE, PUMP_GUARD_BLOCK_NONE);
            }
        }
    }

    /* Return level transition events */
    if (tank->st.return_level_state != prev_ret) {
        if (tank->st.return_level_state == NUTRIENT_TANK_LEVEL_HIGH) {
            nt_push_event(tank, NUTRIENT_TANK_EVENT_RETURN_HIGH, main_ul, ret_ul, NUTRIENT_TANK_ERR_NONE, PUMP_GUARD_BLOCK_NONE);
        }
    }

    /* Request return/refill logic */
    req_return = 0U;
    req_refill = 0U;

    if (tank->cfg.main_level.map_fn != NULL) {
        if (tank->st.main_level_state == NUTRIENT_TANK_LEVEL_LOW ||
            tank->st.main_level_state == NUTRIENT_TANK_LEVEL_CRITICAL) {
            req_return = 1U;

            /* If return tank is not available or has little solution, request refill too. */
            if (tank->cfg.return_level.map_fn == NULL || ret_avail == 0U) {
                req_refill = 1U;
            } else if (tank->cfg.level_policy.return_resume_ul != 0U && ret_ul < tank->cfg.level_policy.return_resume_ul) {
                req_refill = 1U;
            }
        }

        /* Additional trigger: return tank high -> request return */
        if (tank->cfg.return_level.map_fn != NULL && ret_avail != 0U &&
            tank->st.return_level_state == NUTRIENT_TANK_LEVEL_HIGH) {
            req_return = 1U;
        }

        /* If main is too high, do not request return (avoid overflow). */
        if (tank->cfg.level_policy.main_block_return_ul != 0U && main_avail != 0U) {
            if (main_ul >= tank->cfg.level_policy.main_block_return_ul) {
                req_return = 0U;
            }
        }
    }

    if (req_return != 0U && tank->st.request_return_active == 0U) {
        nt_push_event(tank, NUTRIENT_TANK_EVENT_REQUEST_RETURN, main_ul, ret_ul, NUTRIENT_TANK_ERR_NONE, PUMP_GUARD_BLOCK_NONE);
    }
    tank->st.request_return_active = req_return;

    if (req_refill != 0U && tank->st.request_refill_active == 0U) {
        nt_push_event(tank, NUTRIENT_TANK_EVENT_REQUEST_REFILL, main_ul, ret_ul, NUTRIENT_TANK_ERR_NONE, PUMP_GUARD_BLOCK_NONE);
    }
    tank->st.request_refill_active = req_refill;
}

static void nt_apply_circulation_policy(NutrientTank_t *tank, uint32_t now_ms)
{
    if (tank == NULL || tank->cfg.circulation == NULL) {
        return;
    }

    /* If circulation is not requested, ensure pump is off. */
    if (tank->st.circulation_requested == 0U) {
        nt_stop_guard(tank->cfg.circulation);
        return;
    }

    /* If main level does not allow circulation, stop it. */
    if (nt_main_allows_circulation(tank, now_ms) == 0U) {
        nt_stop_guard(tank->cfg.circulation);
        return;
    }

    /* If not running, start a slice. */
    if (tank->cfg.circulation->cfg.pump != NULL &&
        pump_unit_is_running(tank->cfg.circulation->cfg.pump) == 0U) {

        if (pump_guard_start_for_ms(tank->cfg.circulation, now_ms, k_circulation_slice_ms) == 0U) {
            tank->st.last_error = NUTRIENT_TANK_ERR_PUMP_BLOCKED;
            nt_push_event(tank,
                          NUTRIENT_TANK_EVENT_OPERATION_BLOCKED,
                          tank->cfg.main_level.last_volume_ul,
                          tank->cfg.return_level.last_volume_ul,
                          tank->st.last_error,
                          pump_guard_get_block_reason(tank->cfg.circulation));
        }
    }
}

static void nt_handle_active_command(NutrientTank_t *tank, uint32_t now_ms)
{
    PumpGuard_t *guard;
    uint8_t running;
    uint8_t needs_mix;

    if (tank == NULL) {
        return;
    }

    if (tank->st.state == NUTRIENT_TANK_STATE_STOPPED) {
        return;
    }

    /* Start executing pending command when idle. */
    if (tank->st.state == NUTRIENT_TANK_STATE_IDLE && tank->st.has_active_cmd != 0U) {

        tank->st.last_error = NUTRIENT_TANK_ERR_NONE;

        if (tank->st.active_cmd.type == NUTRIENT_TANK_CMD_AERATE_FOR_MS) {

            if (tank->cfg.air == NULL) {
                tank->st.last_error = NUTRIENT_TANK_ERR_INVALID_ARG;
                tank->st.has_active_cmd = 0U;
                return;
            }

            if (pump_guard_start_for_ms(tank->cfg.air, now_ms, tank->st.active_cmd.p.aerate.duration_ms) == 0U) {
                tank->st.last_error = NUTRIENT_TANK_ERR_PUMP_BLOCKED;
                nt_push_event(tank,
                              NUTRIENT_TANK_EVENT_OPERATION_BLOCKED,
                              tank->cfg.main_level.last_volume_ul,
                              tank->cfg.return_level.last_volume_ul,
                              tank->st.last_error,
                              pump_guard_get_block_reason(tank->cfg.air));
                tank->st.has_active_cmd = 0U;
                return;
            }

            tank->st.state = NUTRIENT_TANK_STATE_EXECUTING;
            tank->st.state_started_at_ms = now_ms;
            return;
        }

        if (tank->st.active_cmd.type == NUTRIENT_TANK_CMD_DOSE_VOLUME) {

            guard = nt_get_guard_for_dose(tank, &tank->st.active_cmd);
            if (guard == NULL) {
                tank->st.last_error = NUTRIENT_TANK_ERR_INVALID_ARG;
                tank->st.has_active_cmd = 0U;
                return;
            }

            /* Tank-level policies (in addition to pump guard). */
            if (tank->st.active_cmd.p.dose.kind == NUTRIENT_TANK_DOSE_DRAIN) {
                if (nt_main_allows_drain(tank, now_ms) == 0U) {
                    tank->st.last_error = (tank->cfg.main_level.map_fn != NULL) ? NUTRIENT_TANK_ERR_SENSOR_FAULT : NUTRIENT_TANK_ERR_PUMP_BLOCKED;
                    nt_push_event(tank,
                                  NUTRIENT_TANK_EVENT_OPERATION_BLOCKED,
                                  tank->cfg.main_level.last_volume_ul,
                                  tank->cfg.return_level.last_volume_ul,
                                  tank->st.last_error,
                                  PUMP_GUARD_BLOCK_NONE);
                    tank->st.has_active_cmd = 0U;
                    return;
                }
            } else if (tank->st.active_cmd.p.dose.kind == NUTRIENT_TANK_DOSE_RETURN) {
                if (nt_main_allows_return(tank, now_ms) == 0U) {
                    tank->st.last_error = (tank->cfg.main_level.map_fn != NULL) ? NUTRIENT_TANK_ERR_SENSOR_FAULT : NUTRIENT_TANK_ERR_PUMP_BLOCKED;
                    nt_push_event(tank,
                                  NUTRIENT_TANK_EVENT_OPERATION_BLOCKED,
                                  tank->cfg.main_level.last_volume_ul,
                                  tank->cfg.return_level.last_volume_ul,
                                  tank->st.last_error,
                                  PUMP_GUARD_BLOCK_NONE);
                    tank->st.has_active_cmd = 0U;
                    return;
                }
            } else {
                /* Additions into main tank (water/nutrient/pH) */
                if (nt_main_allows_addition(tank, now_ms) == 0U) {
                    tank->st.last_error = (tank->cfg.main_level.map_fn != NULL) ? NUTRIENT_TANK_ERR_SENSOR_FAULT : NUTRIENT_TANK_ERR_PUMP_BLOCKED;
                    nt_push_event(tank,
                                  NUTRIENT_TANK_EVENT_OPERATION_BLOCKED,
                                  tank->cfg.main_level.last_volume_ul,
                                  tank->cfg.return_level.last_volume_ul,
                                  tank->st.last_error,
                                  PUMP_GUARD_BLOCK_NONE);
                    tank->st.has_active_cmd = 0U;
                    return;
                }
            }

            if (pump_guard_start_for_volume_ul(guard, now_ms, tank->st.active_cmd.p.dose.volume_ul, NULL) == 0U) {
                tank->st.last_error = NUTRIENT_TANK_ERR_PUMP_BLOCKED;
                nt_push_event(tank,
                              NUTRIENT_TANK_EVENT_OPERATION_BLOCKED,
                              tank->cfg.main_level.last_volume_ul,
                              tank->cfg.return_level.last_volume_ul,
                              tank->st.last_error,
                              pump_guard_get_block_reason(guard));
                tank->st.has_active_cmd = 0U;
                return;
            }

            tank->st.state = NUTRIENT_TANK_STATE_EXECUTING;
            tank->st.state_started_at_ms = now_ms;
            return;
        }

        /* Closed-loop commands are not executed in this step */
        if (tank->st.active_cmd.type == NUTRIENT_TANK_CMD_CONTROL_START ||
            tank->st.active_cmd.type == NUTRIENT_TANK_CMD_CONTROL_STOP) {

            tank->st.last_error = NUTRIENT_TANK_ERR_INVALID_ARG;
            nt_push_event(tank,
                          NUTRIENT_TANK_EVENT_CONTROL_ERROR,
                          tank->cfg.main_level.last_volume_ul,
                          tank->cfg.return_level.last_volume_ul,
                          tank->st.last_error,
                          PUMP_GUARD_BLOCK_NONE);
            tank->st.has_active_cmd = 0U;
            return;
        }

        /* Unknown command */
        tank->st.last_error = NUTRIENT_TANK_ERR_INVALID_ARG;
        tank->st.has_active_cmd = 0U;
        return;
    }

    /* State machine: EXECUTING */
    if (tank->st.state == NUTRIENT_TANK_STATE_EXECUTING && tank->st.has_active_cmd != 0U) {

        if (tank->st.active_cmd.type == NUTRIENT_TANK_CMD_AERATE_FOR_MS) {

            if (tank->cfg.air == NULL || tank->cfg.air->cfg.pump == NULL) {
                tank->st.last_error = NUTRIENT_TANK_ERR_INVALID_ARG;
                tank->st.has_active_cmd = 0U;
                tank->st.state = NUTRIENT_TANK_STATE_IDLE;
                return;
            }

            running = pump_unit_is_running(tank->cfg.air->cfg.pump);
            if (running == 0U) {
                /* Aeration finished -> settle */
                if (tank->cfg.timing.after_aerate_settle_ms != 0U) {
                    tank->st.wait_until_ms = now_ms + tank->cfg.timing.after_aerate_settle_ms;
                    tank->st.state = NUTRIENT_TANK_STATE_WAIT_SETTLE;
                } else {
                    tank->st.has_active_cmd = 0U;
                    tank->st.state = NUTRIENT_TANK_STATE_IDLE;
                }
            }

            return;
        }

        if (tank->st.active_cmd.type == NUTRIENT_TANK_CMD_DOSE_VOLUME) {

            guard = nt_get_guard_for_dose(tank, &tank->st.active_cmd);
            if (guard == NULL || guard->cfg.pump == NULL) {
                tank->st.last_error = NUTRIENT_TANK_ERR_INVALID_ARG;
                tank->st.has_active_cmd = 0U;
                tank->st.state = NUTRIENT_TANK_STATE_IDLE;
                return;
            }

            running = pump_unit_is_running(guard->cfg.pump);
            if (running == 0U) {

                /* If guard blocked during execution, report it. */
                if (pump_guard_get_block_reason(guard) != PUMP_GUARD_BLOCK_NONE) {
                    tank->st.last_error = NUTRIENT_TANK_ERR_PUMP_BLOCKED;
                    nt_push_event(tank,
                                  NUTRIENT_TANK_EVENT_OPERATION_BLOCKED,
                                  tank->cfg.main_level.last_volume_ul,
                                  tank->cfg.return_level.last_volume_ul,
                                  tank->st.last_error,
                                  pump_guard_get_block_reason(guard));
                }

                needs_mix = nt_cmd_requires_after_dose_mix(&tank->st.active_cmd);

                if (needs_mix != 0U && tank->cfg.air != NULL &&
                    tank->cfg.timing.after_dose_aerate_ms != 0U) {

                    /* Start aeration after dose */
                    if (pump_guard_start_for_ms(tank->cfg.air, now_ms, tank->cfg.timing.after_dose_aerate_ms) == 0U) {
                        tank->st.last_error = NUTRIENT_TANK_ERR_PUMP_BLOCKED;
                        nt_push_event(tank,
                                      NUTRIENT_TANK_EVENT_OPERATION_BLOCKED,
                                      tank->cfg.main_level.last_volume_ul,
                                      tank->cfg.return_level.last_volume_ul,
                                      tank->st.last_error,
                                      pump_guard_get_block_reason(tank->cfg.air));

                        /* Even if aeration failed, proceed to settle. */
                        tank->st.wait_until_ms = now_ms + tank->cfg.timing.after_dose_settle_ms;
                        tank->st.state = NUTRIENT_TANK_STATE_WAIT_SETTLE;
                        return;
                    }

                    tank->st.state = NUTRIENT_TANK_STATE_AERATE_AFTER_DOSE;
                    return;
                }

                /* No mixing requested -> done */
                tank->st.has_active_cmd = 0U;
                tank->st.state = NUTRIENT_TANK_STATE_IDLE;
            }

            return;
        }

        /* Any other command type -> finish */
        tank->st.has_active_cmd = 0U;
        tank->st.state = NUTRIENT_TANK_STATE_IDLE;
        return;
    }

    /* State machine: AERATE_AFTER_DOSE */
    if (tank->st.state == NUTRIENT_TANK_STATE_AERATE_AFTER_DOSE) {

        if (tank->cfg.air == NULL || tank->cfg.air->cfg.pump == NULL) {
            tank->st.wait_until_ms = now_ms + tank->cfg.timing.after_dose_settle_ms;
            tank->st.state = NUTRIENT_TANK_STATE_WAIT_SETTLE;
            return;
        }

        running = pump_unit_is_running(tank->cfg.air->cfg.pump);
        if (running == 0U) {
            tank->st.wait_until_ms = now_ms + tank->cfg.timing.after_dose_settle_ms;
            tank->st.state = NUTRIENT_TANK_STATE_WAIT_SETTLE;
        }

        return;
    }

    /* State machine: WAIT_SETTLE */
    if (tank->st.state == NUTRIENT_TANK_STATE_WAIT_SETTLE) {

        if (nt_time_reached(now_ms, tank->st.wait_until_ms) != 0U) {
            tank->st.has_active_cmd = 0U;
            tank->st.state = NUTRIENT_TANK_STATE_IDLE;
        }

        return;
    }
}

uint8_t nutrient_tank_init(NutrientTank_t *tank,
                           const NutrientTank_Config_t *cfg,
                           NutrientTank_Event_t *event_buffer,
                           uint8_t event_buffer_len)
{
    if (tank == NULL || cfg == NULL) {
        return 0U;
    }

    memset(tank, 0, sizeof(*tank));
    tank->cfg = *cfg;

    /* Event queue uses user-provided buffer length */
    tank->events = event_buffer;
    tank->cfg.event_queue_size = event_buffer_len;

    tank->st.state = NUTRIENT_TANK_STATE_IDLE;
    tank->st.last_error = NUTRIENT_TANK_ERR_NONE;

    tank->st.main_level_state = NUTRIENT_TANK_LEVEL_OK;
    tank->st.return_level_state = NUTRIENT_TANK_LEVEL_OK;

    tank->st.ev_wr = 0U;
    tank->st.ev_rd = 0U;

    return 1U;
}

void nutrient_tank_reset(NutrientTank_t *tank)
{
    if (tank == NULL) {
        return;
    }

    nt_stop_all_pumps(tank);

    tank->st.state = NUTRIENT_TANK_STATE_IDLE;
    tank->st.last_error = NUTRIENT_TANK_ERR_NONE;

    tank->st.main_level_state = NUTRIENT_TANK_LEVEL_OK;
    tank->st.return_level_state = NUTRIENT_TANK_LEVEL_OK;

    tank->st.circulation_requested = 0U;

    tank->st.has_active_cmd = 0U;
    memset(&tank->st.active_cmd, 0, sizeof(tank->st.active_cmd));

    tank->st.state_started_at_ms = 0U;
    tank->st.wait_until_ms = 0U;

    tank->st.request_return_active = 0U;
    tank->st.request_refill_active = 0U;

    tank->st.ev_wr = 0U;
    tank->st.ev_rd = 0U;
}

void nutrient_tank_process(NutrientTank_t *tank, uint32_t now_ms)
{
    if (tank == NULL) {
        return;
    }

    /* Process all pump guards (includes internal pump timing). */
    nt_process_all_guards(tank, now_ms);

    /* Update level states and generate level/request events. */
    nt_update_level_states_and_events(tank, now_ms);

    /* Apply automatic circulation policy (pause on low/critical). */
    nt_apply_circulation_policy(tank, now_ms);

    /* If main does not allow drain, stop drain pump if it's running. */
    if (tank->cfg.drain != NULL && nt_main_allows_drain(tank, now_ms) == 0U) {
        nt_stop_guard(tank->cfg.drain);
    }

    /* Execute active manual command state machine. */
    nt_handle_active_command(tank, now_ms);
}

uint8_t nutrient_tank_submit_command(NutrientTank_t *tank, const NutrientTank_Command_t *cmd)
{
    if (tank == NULL || cmd == NULL) {
        return 0U;
    }

    if (cmd->type == NUTRIENT_TANK_CMD_EMERGENCY_STOP) {
        nutrient_tank_emergency_stop(tank);
        return 1U;
    }

    /* Circulation command is allowed any time. */
    if (cmd->type == NUTRIENT_TANK_CMD_CIRCULATION_SET) {
        tank->st.circulation_requested = (cmd->p.circulation.enable != 0U) ? 1U : 0U;

        if (tank->st.circulation_requested == 0U && tank->cfg.circulation != NULL) {
            nt_stop_guard(tank->cfg.circulation);
        }

        return 1U;
    }

    /* Single active command policy (no queue). */
    if (tank->st.has_active_cmd != 0U ||
        (tank->st.state != NUTRIENT_TANK_STATE_IDLE && tank->st.state != NUTRIENT_TANK_STATE_WAIT_SETTLE)) {
        tank->st.last_error = NUTRIENT_TANK_ERR_BUSY;
        return 0U;
    }

    tank->st.active_cmd = *cmd;
    tank->st.has_active_cmd = 1U;

    return 1U;
}

void nutrient_tank_update_main_distance_mm(NutrientTank_t *tank, uint32_t now_ms, uint32_t distance_mm)
{
    uint32_t volume_ul;

    if (tank == NULL) {
        return;
    }

    if (tank->cfg.main_level.map_fn == NULL) {
        return;
    }

    volume_ul = tank->cfg.main_level.map_fn(tank->cfg.main_level.map_ctx, distance_mm);

    tank->cfg.main_level.last_distance_mm = distance_mm;
    tank->cfg.main_level.last_volume_ul = volume_ul;
    tank->cfg.main_level.last_update_ms = now_ms;

    tank->cfg.main_level.valid = 1U;
    tank->cfg.main_level.fault = 0U;
}

void nutrient_tank_set_main_sensor_fault(NutrientTank_t *tank, uint32_t now_ms)
{
    (void)now_ms;

    if (tank == NULL) {
        return;
    }

    if (tank->cfg.main_level.map_fn == NULL) {
        return;
    }

    tank->cfg.main_level.fault = 1U;
    tank->cfg.main_level.valid = 0U;
}

void nutrient_tank_update_return_distance_mm(NutrientTank_t *tank, uint32_t now_ms, uint32_t distance_mm)
{
    uint32_t volume_ul;

    if (tank == NULL) {
        return;
    }

    if (tank->cfg.return_level.map_fn == NULL) {
        return;
    }

    volume_ul = tank->cfg.return_level.map_fn(tank->cfg.return_level.map_ctx, distance_mm);

    tank->cfg.return_level.last_distance_mm = distance_mm;
    tank->cfg.return_level.last_volume_ul = volume_ul;
    tank->cfg.return_level.last_update_ms = now_ms;

    tank->cfg.return_level.valid = 1U;
    tank->cfg.return_level.fault = 0U;
}

void nutrient_tank_set_return_sensor_fault(NutrientTank_t *tank, uint32_t now_ms)
{
    (void)now_ms;

    if (tank == NULL) {
        return;
    }

    if (tank->cfg.return_level.map_fn == NULL) {
        return;
    }

    tank->cfg.return_level.fault = 1U;
    tank->cfg.return_level.valid = 0U;
}

uint8_t nutrient_tank_pop_event(NutrientTank_t *tank, NutrientTank_Event_t *ev_out)
{
    if (tank == NULL || ev_out == NULL || tank->events == NULL || tank->cfg.event_queue_size == 0U) {
        return 0U;
    }

    if (tank->st.ev_rd == tank->st.ev_wr) {
        return 0U;
    }

    *ev_out = tank->events[tank->st.ev_rd];

    tank->st.ev_rd++;
    if (tank->st.ev_rd >= tank->cfg.event_queue_size) {
        tank->st.ev_rd = 0U;
    }

    return 1U;
}

void nutrient_tank_emergency_stop(NutrientTank_t *tank)
{
    if (tank == NULL) {
        return;
    }

    nt_stop_all_pumps(tank);

    tank->st.circulation_requested = 0U;
    tank->st.has_active_cmd = 0U;

    tank->st.state = NUTRIENT_TANK_STATE_STOPPED;
    tank->st.last_error = NUTRIENT_TANK_ERR_NONE;
}
