// main.c
...

// Mount the filesystem
FRESULT res;
res = f_mount(&USERFatFS, USERPath, 1);
if (res != FR_OK)
{
    while (1)
    {
        HAL_GPIO_TogglePin(LED_INDICATOR_GPIO_Port, LED_INDICATOR_Pin);
        HAL_Delay(1000);
    }
}

FIL status_file;
const TCHAR *status_file_path = "status.txt";

res = f_open(&status_file, status_file_path, FA_OPEN_APPEND | FA_WRITE);
if (res != FR_OK)
{
    while (1)
    {
        HAL_GPIO_TogglePin(LED_INDICATOR_GPIO_Port, LED_INDICATOR_Pin);
        HAL_Delay(500);
    }
}
const TCHAR *file_text = "Hello, world\n";
UINT text_size = strlen(file_text);
UINT written_size;

res = f_write(&status_file, file_text, text_size, &written_size);
if (res != FR_OK)
{
    while (1)
    {
        HAL_GPIO_TogglePin(LED_INDICATOR_GPIO_Port, LED_INDICATOR_Pin);
        HAL_Delay(500);
    }
}

f_close(&status_file);

//  FIL file;
//  FRESULT res;
//  res = f_open(&file, "test.txt", FA_READ);
//  if (res != FR_OK)
//  {
//	  while (1)
//	  {
//		  HAL_GPIO_TogglePin(LED_INDICATOR_GPIO_Port, LED_INDICATOR_Pin);
//		  HAL_Delay(500);
//	  }
//  }
//  f_close(&file);

...