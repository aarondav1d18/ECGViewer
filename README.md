# ECG Project

This project is being done as part of a team project at the University of Glasgow.

Contributors:

[Aaron David](@aarondav1d18)

## License

This project is licensed under the GNU General Public License v3.0 or later.
See the LICENSE file for details.


## Current State 

The current state of the `ECG_plotter`:
- The code will attempt to import the C++ parser
    - If unsuccessful it will default to the python version
- Reads in all the data from the txt file
- Plots this data on a matplotlib graph
    - allows for a set window and scrolling through this window
- removes baseline drift and plots the baseline on the graph
- Searches for pacing noise stores this interval and highlights in a graph
- Adds artifical noise in place visually of the noise
- Detects QRS
- Start of detection for T and P wave

## Libaries

Below is a list of the main libaries that have been used throughout the code and what they have been used for.

- matplotlib - used for plotting the data and visualisings the data / algorithms
- numpy - used for the algroithms and processing of large data
- pandas - used to speed up the reading of the data as these files can be quite large
- scipy - used mostly for wave detection and algorithms to help with the algorihtms and detection of key points
- pybinds11 - used to compile the C++ code into a usable python libary
- qcustomplot - used for custom functionallity in qt. Original github - https://github.com/vasilyaksenov/QCustomPlot?tab=readme-ov-file


## Data

The data used for this project is the txt files. There is currently no way to input a csv. The txt files should be formatted as follows:

A section at the top for the meta data of the readings:
```plaintext
Interval=	0.00025 s
ExcelDateTime=	4.5778631066601200e+004	01/05/2025 15:08:44.154344
TimeFormat=	StartOfBlock
DateFormat=	
ChannelTitle=	ECG
Range=	10.000 V
```

Then directly bellow this is the data used for the plotting and algorithms. This is as follows:
```plaintext
2054.125	0.49375
2054.12525	0.49375
2054.1255	0.49375
2054.12575	0.4934375
2054.126	0.49375
2054.12625	0.494375
2054.1265	0.494375
2054.12675	0.4940625
```
The data is the in the format of `Voltage time`

## Compiling C++

This compiling guide has been done on linux and will work for macbooks. If you are using a windows pc you will need to point cmake to where pybinds and qt are installed on your pc.

### Linux

In the terminal navigate to the `ECGViewer` folder in this repo and run the following commands:

```bash
mkdir build && cd build
```
```bash
cmake ..
```
```
make
```

This should then compile and produce a `parseECG*.so` file which is the compilied python libary.

### Build.bash

If you are on linux or mac there is a bash file that will take care of building the code and cleaning up the build files. This was made to avoid having to run multiple commands each time you build and also to allow the CI pipeline for the build stage to have less commands defined.

To run this script navigate to the `ECGViewer` folder in the terminal and you can run any of the following:

```bash
bash build.bash
```
> This will run the standard build commands detailed above

```bash
bash build.bash --clean
```
> This command will clean up and remove all the build files and the .so file produced from building the source code.

```bash
bash build.bash --clean-build
```
> This command will remove all build files before building again. The idea is if you want a fresh build this command is an easy way to do this.


## Visualisation 

To open the file selection gui you can run the `main.py` file. This will deal with linking the rest of the code. To select a file click browse and find the data file you would like to use. 

### Linux 
```bash
python3 main.py
```
