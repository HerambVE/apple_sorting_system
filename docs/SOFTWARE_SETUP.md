# 💻 Software Setup Guide

## Cloud Server (Google Colab)

1. Open `cloud-server/optimus_prime.ipynb` in Google Colab.
2. Ensure Runtime Type is set to **GPU** (T4 or V100).
3. The notebook requires an `ngrok` account to expose the Flask API. Get an authtoken from [ngrok.com](https://ngrok.com/).
4. Paste your ngrok authtoken in the specified cell.
5. Run all cells. The notebook will:
   - Download the Kaggle dataset (`anilsandhii/apple-fruit-disease-images-dataset`).
   - Train the `vit_base_patch16_224` model.
   - Start the Flask server on port 5000 and expose it via ngrok.
6. Look for the output `* Running on http://<random-id>.ngrok.io`. Copy this URL.

## Raspberry Pi Controller

### Prerequisites
You need the `pigpio` library and `libcurl` installed:
```bash
sudo apt-get update
sudo apt-get install pigpio pigpio-tools libcurl4-openssl-dev cmake fswebcam
```
*Note: We use `fswebcam` for the USB camera and `rpicam-jpeg` for the CSI camera.*

### Compilation
Navigate to the `raspberry-pi` directory and build the executable:
```bash
cd raspberry-pi
mkdir -p build
cd build
cmake ..
make
```

### Running the System
The program requires root privileges to use `pigpio` hardware PWM.
```bash
# Start the pigpio daemon if not already running
sudo pigpiod

# Run the sorter, replacing with your ngrok URL
sudo ./applesortv2 https://<your-ngrok-url>.ngrok.io
```

The system will start in an idle state. It supports `AUTO` and `MANUAL` modes.
