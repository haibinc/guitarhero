#include "max7219.h"

static void CS_Low(void)  { HAL_GPIO_WritePin(MAX7219_CS_PORT, MAX7219_CS_PIN, GPIO_PIN_RESET); }
static void CS_High(void) { HAL_GPIO_WritePin(MAX7219_CS_PORT, MAX7219_CS_PIN, GPIO_PIN_SET); }

void MAX7219_WriteAll(SPI_HandleTypeDef *hspi, uint8_t reg, uint8_t data)
{
    CS_Low();
    for (uint8_t i = 0; i < MAX7219_NUM_DEVICES; i++)
    {
        uint8_t tx[2] = { reg, data };
        HAL_SPI_Transmit(hspi, tx, 2, HAL_MAX_DELAY);
    }
    CS_High();
}

void MAX7219_Init(SPI_HandleTypeDef *hspi)
{
    CS_High();

    MAX7219_WriteAll(hspi, MAX7219_REG_DISPLAYTEST, 0x00);
    MAX7219_WriteAll(hspi, MAX7219_REG_DECODEMODE, 0x00);
    MAX7219_WriteAll(hspi, MAX7219_REG_SCANLIMIT, 0x07);
    MAX7219_WriteAll(hspi, MAX7219_REG_INTENSITY, 0x04);
    MAX7219_WriteAll(hspi, MAX7219_REG_SHUTDOWN, 0x01);

    MAX7219_Clear(hspi);
}

void MAX7219_SetIntensity(SPI_HandleTypeDef *hspi, uint8_t intensity)
{
    MAX7219_WriteAll(hspi, MAX7219_REG_INTENSITY, intensity & 0x0F);
}

void MAX7219_Clear(SPI_HandleTypeDef *hspi)
{
    for (uint8_t col = 1; col <= 8; col++)
    {
        MAX7219_WriteAll(hspi, col, 0x00);
    }
}

// Last device's data goes out FIRST - it gets shifted furthest down the chain.
void MAX7219_SetColumn(SPI_HandleTypeDef *hspi, uint8_t column, const uint8_t *values)
{
    CS_Low();
    for (int8_t i = MAX7219_NUM_DEVICES - 1; i >= 0; i--)
    {
        uint8_t tx[2] = { column, values[i] };
        HAL_SPI_Transmit(hspi, tx, 2, HAL_MAX_DELAY);
    }
    CS_High();
}

void MAX7219_DrawFrame(SPI_HandleTypeDef *hspi, const uint8_t *frame)
{
    // Absolute column index -> (device, register) is:
    //   device   = absCol / 8
    //   register = (absCol % 8) + 1
    for (uint8_t reg = 1; reg <= 8; reg++)
    {
        uint8_t colData[MAX7219_NUM_DEVICES];
        for (uint8_t dev = 0; dev < MAX7219_NUM_DEVICES; dev++)
        {
            colData[dev] = frame[dev * 8 + (reg - 1)];
        }
        MAX7219_SetColumn(hspi, reg, colData);
    }
}
