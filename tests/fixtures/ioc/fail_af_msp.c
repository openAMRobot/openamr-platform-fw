/* Fixture (fail_af): UART4_RX on PB8 with AF4 instead of AF8. */
    GPIO_InitStruct.Pin = GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF4_UART4;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
