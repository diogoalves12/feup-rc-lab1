# **RCOM Project**

### **Project Structure**

#### `bin/`
Contains the compiled binaries (executables).

#### `src/`
Includes the source code files for the implementation of the **link layer** and **application layer** protocols. Students should edit these files to implement their solution.

#### `cable/`
Contains the virtual cable program used to emulate and test serial port communication. This file **must not be changed**.

#### `main.c`
The main file of the application. This file **should not be edited**.

#### `Makefile`
Makefile to build the project and run the application.

#### `penguin.gif`
Example file to be sent through the serial port.

## Build & Run

### Requirements
- `gcc`, `make`, and `socat` installed and available in `PATH`.
- Linux: `sudo apt-get update && sudo apt-get install -y build-essential socat`

### Build
- Build (src + cable): `make`

Note:
- If `bin/cable` is missing, force a rebuild with one of:
  - `make -B cable`
  - touch the source then build: `touch cable/cable.c && make cable`

### Start the Virtual Cable
Run the cable program: `sudo make run_cable` or  `sudo ./bin/cable`

Useful cable console commands: `help`, `on`, `off`, `ber <val>`, `baud <rate>`, `prop <usec>`, `log <file>`, `endlog`, `quit`.

### Run Receiver and Transmitter
- In a terminal (receiver):
  - `make run_rx` or `./bin/main /dev/ttyS11 9600 rx penguin-received.gif`
- In another terminal (transmitter):
  - `make run_tx` or `./bin/main /dev/ttyS10 9600 tx penguin.gif`

### Verify files
- Compare files: - `make check_files` or `diff -s penguin.gif penguin-received.gif`

