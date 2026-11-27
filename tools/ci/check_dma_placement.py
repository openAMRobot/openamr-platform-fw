#!/usr/bin/env python3
"""Backward-compatible entry point for the complete STM32H723 memory check."""

from check_stm32_memory import main


if __name__ == "__main__":
    raise SystemExit(main())
