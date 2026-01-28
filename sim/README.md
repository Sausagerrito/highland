# Simulation

The idea here is to create a simulation that can pretend to be sensor data for when we don't have access to the actual thermocouples.

WARNING: You have to change the USB address in main to match that of the port your Arduino is connected to. The exact syntax may very between Windows, MacOS and Linux.

To run it you just type
```zsh
python main.py
```

However, you need to be running the right version of python and have access to the pyserial library. The way to do this is to start a virtual environment, which is a python install located in the project folder itself, instead of your global python install.

If you have python installed, you can create a virtual environment, and install pyserial in the folder with
```zsh
python -m venv .venv
pip install pyserial
```
To start it, while in the project directory run
```zsh
source .venv/bin/activate
```
and to turn it off its just
```zsh
deactivate
```
