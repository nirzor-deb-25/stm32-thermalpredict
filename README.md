# ThermalPredict

Embedded temperature forecasting using an STM32 NUCLEO-F439ZI
and an Olimex MOD-BME280 sensor.

Temperature is sampled approximately once per second.
A ridge regression model predicts temperature 30 seconds ahead.
Training runs on a computer; inference runs on the STM32.

## Wiring

| BME280 | NUCLEO-F439ZI |
|---|---|
| VDD | 3V3 |
| GND | GND |
| SCL | PB8 / D15 |
| SDA | PB9 / D14 |

Sensor address: 0x77. Verified chip ID: 0x60.
Disconnect USB before changing wiring. Use 3.3 V power.

Serial output: USART3 through ST-LINK VCP, 115200 baud, 8N1.
Development port: COM7; this may differ on another computer.

## Forecasting model

Inputs are temperature changes over the preceding 5, 15 and 30 seconds.
The model predicts a change, which is added to the current temperature.

Training: train02. Validation: validation01. Final test: test01.
Scaling and model parameters were fitted only on training data.
V2 was selected using validation results.
The final test was evaluated without retraining.

Model ID: fbd153500b17f938.

## Final pilot test

| Method | MAE C | RMSE C | Maximum absolute error C |
|---|---:|---:|---:|
| Persistence baseline | 2.462 | 3.088 | 8.93 |
| Ridge v2 | 1.889 | 2.611 | 8.62 |

Test MAE reduction: approximately 23.3%.
Test examples: 357 overlapping forecasts from one recording.

## Device verification

- Python-reference model checks: 1072 cases, zero failures.
- Numerical comparison tolerance: 1e-9 degrees C.
- Runtime checks: 164 checks, zero failures.
- Runtime tests cover warm-up, buffer wrap, timestamp wrap,
  invalid readings and recovery after timing gaps.
- First live forecast appears after 31 samples, spanning 30 seconds.
- Observed runtime counters: 126 forecasts, zero history resets.
- Maximum observed feature/prediction computation: 407 microseconds.
  Sensor communication and UART output are excluded.
- Nominal clock: 16 MHz HSI. Software floating point.
- Build sections: text 53992 bytes, data 0, BSS 1736 bytes.
  Reference self-tests are included. BSS excludes runtime stack use.

## Repository structure

- Src, Inc: drivers, measurement, model, runtime and self-tests.
- Startup: MCU startup assembly.
- host: training, evaluation, export and reference generation.
- data/raw: selected reproducibility recordings.
- models: model parameters, metrics and prediction files.
- docs/evaluation: final test report and predictions.
- docs/device: captured device output, when available.
- record_thermal.ps1: guided seven-minute recorder.

## Build

Run in Windows PowerShell with STM32 tools installed:

    .\setup.ps1
    make all

Firmware output: build/stm32_i2c_bme280.elf.
This filename is retained from the initial sensor project.

Flashing replaces the target board's firmware.
Explicitly select the NUCLEO ST-LINK serial number.
Development NUCLEO serial: 066EFF555380535067043227.

## Python environment and evaluation

Python 3.13 was used during development.
Select an actual Python executable installed on your computer.
Create a virtual environment, then run:

    .\.venv\Scripts\python.exe -m pip install -r requirements.txt
    .\.venv\Scripts\python.exe .\host\evaluate_thermal.py

Additional scripts:

- train_thermal.py: reproduce v1 training.
- train_thermal_v2.py: reproduce v2 training.
- export_thermal_model.py: export the evaluated model to C.
- generate_thermal_selftest.py: generate Python-reference model cases.

Training refuses to overwrite existing model output folders.
Reproduce training in a separate checkout without those output folders.
The exporter checks the exact evaluated model hash.
Numerical differences between environments can change a reproduced hash;
investigate and evaluate before modifying the hash guard.

## Limitations

This is a hand-warming pilot demonstration.
It is not industrial validation or a safety control system.
Each data split contains only one recording.
Overlapping forecast examples are correlated.
Unseen future heating changes cannot always be inferred from history.

An earlier exploratory recording had unavailable temperature readings.
Their underlying cause was not established. Diagnostics remain enabled.
Selected training, validation and test recordings have no read errors.
Invalid samples and timing gaps clear the live forecast history.

The sensor was not calibrated against an independent thermometer.
Output resolution of 0.01 C does not imply that measurement accuracy.
Forecasts are not clipped.
