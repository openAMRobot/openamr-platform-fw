#!/usr/bin/env python3
"""Compare a STM32CubeMX .ioc with the controller I/O contract of openamr-platform-hw.

The contract is interfaces/electrical/base-controller-io.yaml in openamr-platform-hw, fetched at
a pinned SHA by the workflow. Parsed from the .ioc: PXn.Signal, PXn.GPIO_Label, PXn.PinState,
UART/USART BaudRate, WordLength, Parity, StopBits, Mode and RxPinLevelInvertParam, SPI
CLKPolarity, CLKPhase and BaudRatePrescaler with the SPI kernel clock, FDCAN NominalPrescaler,
NominalTimeSeg1 and NominalTimeSeg2 with the FDCAN kernel clock, and the ETH pins. Unset
parameters take the CubeMX default (115200 baud, 8 bits, no parity, 1 stop bit, TX and RX, no
inversion, SPI Mode 0). AF numbers are not stored in a .ioc; when the CubeMX-generated
Core/Src/stm32h7xx_hal_msp.c is next to the .ioc (or given with --msp), the AF of every pin is
read from it.

Errors (exit 1):
  pin            a contract signal configured on a different pin, or with a different AF
  not-required   a not_required pin still configured with an alternate function
  reserved       a reserved board pin configured for another function
  setting        a baud, frame, inversion, mode, SPI mode or clock, CAN bitrate or GPIO idle
                 level that differs from the contract
  missing        a configured signal (pin with a Signal) that is not in the contract
Warnings: contract signals with a pin that the .ioc does not configure (NOT CONFIGURED).
The check proves the .ioc agrees with the contract text, not that the board is wired so.
"""
import argparse
import hashlib
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

import yaml

PIN = re.compile(r"^(P[A-K](?:1[0-5]|[0-9]))(?=$|[-(])")
UART = re.compile(r"^U(?:S)?ART\d+$|^LPUART\d+$")
DEFAULTS = {"BaudRate": "115200", "WordLength": "WORDLENGTH_8B", "Parity": "PARITY_NONE",
            "StopBits": "STOPBITS_1", "Mode": "MODE_TX_RX",
            "RxPinLevelInvertParam": "UART_ADVFEATURE_RXINV_DISABLE",
            "CLKPolarity": "SPI_POLARITY_LOW", "CLKPhase": "SPI_PHASE_1EDGE"}
WORD = {"WORDLENGTH_7B": "7", "WORDLENGTH_8B": "8", "WORDLENGTH_9B": "9"}
PARITY = {"PARITY_NONE": "N", "PARITY_EVEN": "E", "PARITY_ODD": "O"}
STOP = {"STOPBITS_0_5": "0.5", "STOPBITS_1": "1", "STOPBITS_1_5": "1.5", "STOPBITS_2": "2"}
MODE = {"MODE_RX": "rx_only", "MODE_TX": "tx_only", "MODE_TX_RX": "tx_rx"}
SPI_MODE = {("SPI_POLARITY_LOW", "SPI_PHASE_1EDGE"): 0, ("SPI_POLARITY_LOW", "SPI_PHASE_2EDGE"): 1,
            ("SPI_POLARITY_HIGH", "SPI_PHASE_1EDGE"): 2, ("SPI_POLARITY_HIGH", "SPI_PHASE_2EDGE"): 3}
SPI_CLOCK = {"SPI1": "SPI123", "SPI2": "SPI123", "SPI3": "SPI123", "SPI4": "SPI45", "SPI5": "SPI45", "SPI6": "SPI6"}


def parse_ioc(text):
    """Return (params, pins): params maps 'IP.Key' -> value; pins maps PXn -> {Signal, Label, ...}."""
    params, pins = {}, defaultdict(dict)
    for raw in text.splitlines():
        if not raw or raw.startswith("#") or "=" not in raw:
            continue
        key, value = raw.split("=", 1)
        value = value.replace("\\:", ":").replace("\\#", "#").strip()
        params[key] = value
        name, _, field = key.partition(".")
        m = PIN.match(name)
        if m and field in ("Signal", "GPIO_Label", "PinState", "Mode", "Locked"):
            pins[m.group(1)][field] = value
    return params, dict(pins)


def parse_msp(text):
    """Return {PXn: AF number} from CubeMX-generated HAL_*_MspInit code."""
    afs = {}
    block = []
    for line in text.splitlines():
        block.append(line)
        m = re.search(r"HAL_GPIO_Init\(\s*GPIO([A-K])\s*,", line)
        if not m:
            continue
        joined = "\n".join(block)
        pins = re.findall(r"GPIO_PIN_(\d+)", joined.split("GPIO_InitStruct.Pin", 1)[-1].split(";", 1)[0]) \
            if "GPIO_InitStruct.Pin" in joined else []
        af = re.findall(r"GPIO_InitStruct\.Alternate\s*=\s*GPIO_AF(\d+)_", joined)
        if pins and af:
            for p in pins:
                afs[f"P{m.group(1)}{p}"] = int(af[-1])
        block = []
    return afs


def peripheral_of(signal):
    return signal.split("_", 1)[0] if "_" in signal else signal


def uart_settings(params, ip):
    get = lambda k: params.get(f"{ip}.{k}", DEFAULTS[k])
    frame = f"{WORD.get(get('WordLength'), '?')}{PARITY.get(get('Parity'), '?')}{STOP.get(get('StopBits'), '?')}"
    return {"baud": int(float(get("BaudRate"))), "frame": frame,
            "rx_invert": get("RxPinLevelInvertParam") == "UART_ADVFEATURE_RXINV_ENABLE",
            "mode": MODE.get(get("Mode"), get("Mode"))}


def spi_settings(params, ip):
    pol = params.get(f"{ip}.CLKPolarity", DEFAULTS["CLKPolarity"])
    pha = params.get(f"{ip}.CLKPhase", DEFAULTS["CLKPhase"])
    out = {"spi_mode": SPI_MODE.get((pol, pha))}
    pre = params.get(f"{ip}.BaudRatePrescaler")
    kernel = params.get(f"RCC.{SPI_CLOCK.get(ip, ip)}Freq_Value")
    if pre and kernel:
        out["clock_hz"] = int(float(kernel)) // int(pre.rsplit("_", 1)[-1])
    return out


def fdcan_bitrate(params, ip):
    try:
        pre = int(params[f"{ip}.NominalPrescaler"])
        seg1 = int(params[f"{ip}.NominalTimeSeg1"])
        seg2 = int(params[f"{ip}.NominalTimeSeg2"])
        kernel = int(float(params["RCC.FDCANFreq_Value"]))
    except (KeyError, ValueError):
        return None
    return kernel / (pre * (1 + seg1 + seg2))


def reserved_ok(entry, signal):
    """A reserved pin may carry only its own function (e.g. SWDIO in DEBUG_JTMS-SWDIO)."""
    if entry["function"] in signal:
        return True
    return entry["peripheral"] == "BOARD" and signal.startswith("GPIO_")


def check(ioc_text, contract, msp_text=None):
    params, pins = parse_ioc(ioc_text)
    afs = parse_msp(msp_text) if msp_text else {}
    findings, warnings = [], []
    entries = contract["signals"]
    by_gpio = defaultdict(list)
    for e in entries:
        if e["gpio"]:
            by_gpio[e["gpio"]].append(e)
    active = [e for e in entries if e["gpio"] and e["status"] in ("verified", "proposed", "unchecked")]

    # pin: contract signal elsewhere, or with another AF
    configured = {(p, d.get("Signal")) for p, d in pins.items() if d.get("Signal")}
    by_signal = defaultdict(list)
    for p, s in configured:
        by_signal[s].append(p)
    for e in active:
        got = pins.get(e["gpio"], {}).get("Signal")
        if got == e["function"]:
            if e["af"] is not None and e["gpio"] in afs and afs[e["gpio"]] != e["af"]:
                findings.append(("pin", f"{e['signal']}: {e['function']} on {e['gpio']} uses AF{afs[e['gpio']]}, "
                                        f"contract AF{e['af']}"))
            continue
        elsewhere = [p for p in by_signal.get(e["function"], []) if p != e["gpio"]] \
            if not e["function"].startswith("GPIO_") else []
        if elsewhere:
            findings.append(("pin", f"{e['signal']}: {e['function']} is on {', '.join(sorted(elsewhere))} in the .ioc, "
                                    f"contract {e['gpio']}"))
        elif got is None:
            warnings.append(f"NOT CONFIGURED: {e['signal']} ({e['function']} on {e['gpio']}) is not set in the .ioc")
        elif not any(x["status"] in ("not_required", "reserved") for x in by_gpio[e["gpio"]]):
            findings.append(("pin", f"{e['signal']}: {e['gpio']} carries {got} in the .ioc, contract {e['function']}"))

    for pin, data in sorted(pins.items()):
        signal = data.get("Signal")
        if not signal:
            continue
        here = by_gpio.get(pin, [])
        reserved = [e for e in here if e["status"] == "reserved"]
        spare = [e for e in here if e["status"] == "not_required"]
        if spare and not signal.startswith("GPIO_"):
            findings.append(("not-required", f"{pin} carries {signal} (alternate function"
                                             f"{', AF' + str(afs[pin]) if pin in afs else ''}); contract "
                                             f"{spare[0]['signal']} is not_required: remove the pin from the .ioc"))
        if reserved and not reserved_ok(reserved[0], signal):
            findings.append(("reserved", f"{pin} carries {signal}; the pin is reserved for "
                                         f"{reserved[0]['signal']} ({reserved[0]['device']})"))
        if not any(e["function"] == signal for e in here) and not reserved and not spare:
            findings.append(("missing", f"{pin} carries {signal} in the .ioc, which is not in the contract"))

    # settings per peripheral
    for ip in sorted({e["peripheral"] for e in active}):
        group = [e for e in active if e["peripheral"] == ip and pins.get(e["gpio"], {}).get("Signal") == e["function"]]
        if not group:
            continue
        want = group[0]["settings"]
        if UART.match(ip):
            got = uart_settings(params, ip)
            for key in ("baud", "frame", "rx_invert", "mode"):
                if key in want and want[key] != got[key]:
                    findings.append(("setting", f"{ip}: {key} {got[key]} in the .ioc, contract {want[key]}"))
        elif ip.startswith("SPI"):
            got = spi_settings(params, ip)
            if "spi_mode" in want and got["spi_mode"] != want["spi_mode"]:
                findings.append(("setting", f"{ip}: SPI mode {got['spi_mode']} in the .ioc, contract Mode {want['spi_mode']}"))
            if "max_clock_hz" in want:
                if "clock_hz" not in got:
                    findings.append(("setting", f"{ip}: SPI clock cannot be computed (prescaler or kernel clock unset)"))
                elif got["clock_hz"] > want["max_clock_hz"]:
                    findings.append(("setting", f"{ip}: SPI clock {got['clock_hz']} Hz above the contract maximum "
                                                f"{want['max_clock_hz']} Hz"))
        elif ip.startswith("FDCAN") and "bitrate" in want:
            rate = fdcan_bitrate(params, ip)
            if rate is None:
                findings.append(("setting", f"{ip}: bitrate cannot be computed (prescaler, segments or FDCAN kernel clock unset)"))
            elif abs(rate - want["bitrate"]) > 1e-6 * want["bitrate"]:
                findings.append(("setting", f"{ip}: bitrate {rate:g} bit/s in the .ioc, contract {want['bitrate']}"))
    for e in active:
        level = e["settings"].get("idle_level")
        state = pins.get(e["gpio"], {}).get("PinState")
        if level and pins.get(e["gpio"], {}).get("Signal") == e["function"]:
            got = "high" if state == "GPIO_PIN_SET" else "low"
            if got != level:
                findings.append(("setting", f"{e['signal']}: {e['gpio']} starts {got} in the .ioc, contract idle {level}"))
    return findings, warnings, {p: d.get("GPIO_Label") for p, d in pins.items() if d.get("GPIO_Label")}


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("ioc", help="CubeMX .ioc file")
    p.add_argument("--contract", required=True, help="base-controller-io.yaml from openamr-platform-hw")
    p.add_argument("--contract-sha", default="", help="openamr-platform-hw SHA the contract was read at")
    p.add_argument("--msp", help="generated stm32h7xx_hal_msp.c (default: Core/Src next to the .ioc)")
    p.add_argument("--json", help="write findings as JSON here")
    a = p.parse_args(argv)
    ioc = Path(a.ioc)
    contract_path = Path(a.contract)
    contract = yaml.safe_load(contract_path.read_text(encoding="utf-8"))
    msp = Path(a.msp) if a.msp else ioc.parent / "Core" / "Src" / "stm32h7xx_hal_msp.c"
    msp_text = msp.read_text(encoding="utf-8", errors="replace") if msp.is_file() else None
    findings, warnings, labels = check(ioc.read_text(encoding="utf-8", errors="replace"), contract, msp_text)
    digest = hashlib.sha256(contract_path.read_bytes()).hexdigest()
    print(f"contract {contract.get('contract_version')} (sha256 {digest[:12]}"
          f"{', openamr-platform-hw ' + a.contract_sha if a.contract_sha else ''}); "
          f"AF source: {msp if msp_text else 'none (no generated MSP file; AFs not compared)'}")
    for w in warnings:
        print(f"::warning file={ioc}::{w}")
    for rule, message in findings:
        print(f"::error file={ioc}::[{rule}] {message}")
    print(f"{'FAIL' if findings else 'PASS'} {ioc}: {len(findings)} errors, {len(warnings)} not configured")
    if a.json:
        Path(a.json).write_text(json.dumps({"ioc": str(ioc), "contract_version": contract.get("contract_version"),
                                            "contract_sha256": digest, "contract_sha": a.contract_sha or None,
                                            "result": "FAIL" if findings else "PASS",
                                            "errors": [{"rule": r, "message": m} for r, m in findings],
                                            "not_configured": warnings, "labels": labels}, indent=2) + "\n",
                                encoding="utf-8")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
