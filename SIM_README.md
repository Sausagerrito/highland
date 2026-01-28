# Simulation

The idea here is to create a simulation that can pretend to be sensor data for when we don't have access to the actual thermocouples.

To run it you just type
```zsh
python main.py
```

However, you need to be running the right version of python and have access to the pyserial library. The way to do this is to start a virtual environment, which is a python install located in the project folder itself, instead of your global python install.

To start it, while in the project directory run
```zsh
source venv/bin/activate
```
and to turn it off its just
```zsh
deactivate
```
