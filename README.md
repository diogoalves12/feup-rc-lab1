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

## Manual Measurement Workflow

1. Start the cable console in terminal A (`make && make run_cable`) and configure the run parameters interactively (`ber`, `prop`, `baud`).
2. Launch the receiver in terminal B: `/usr/bin/time -f "%e" -o rx_time.txt ./bin/main /dev/ttyS11 <BAUD> rx penguin-received.gif`.
3. Launch the transmitter in terminal C: `/usr/bin/time -f "%e" -o tx_time.txt ./bin/main /dev/ttyS10 <BAUD> tx penguin.gif`.
4. After both terminals finish, record the experiment with `python3 add_result.py SCEN BAUD PROP_US BER PAYLOAD_B RUN_INDEX [OVERHEAD_BYTES] [LBYTES_HINT]` or run the guided helper `./run_once.sh`.
5. Repeat for every point in the FER / PROP / BAUD / PAYLOAD sweeps (3 runs per point), then run `python3 plot_efficiency.py` to produce the four report figures.
