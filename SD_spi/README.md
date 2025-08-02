# Project configuration
1. Enable SPI port.
    - Change `DMA settings`. Add `SPI_RX` and `SPI_TX` requests.
    - Add `CS` pin.
    - Rename SPI pins:
        - `SD_CS`
        - `SD_SCK`
        - `SD_MISO`
        - `SD_MOSI`
    - Set `SD_SPI_HANDLE`. Example: `#define SD_SPI_HANDLE hspi2`
2. Enable FATFS.
    - `USE_LFN (Use Long Filename)` = `Enable LFN with static working buffer on the BSS`
    - `MIN_SS` = 512
    - `MAX_SS` = 4096
    - `FS_LOCK` = 5
3. Copy `user_diskio_spi.c` and `user_diskio_spi.h` to the `./FATFS/Target/`.
4. Update `./FATFS/Target/user_diskio.c` functions(`USER_initialize()`, `USER_status()`, `USER_read()`, `USER_write()`, `USER_ioctl()`) just like it is done in this local file.

## Note: if some operation failed, set normal speed slower in the `user_diskio_spi.c`
