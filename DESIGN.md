# CS 370 Term Project - Design Document

## 1. System Architecture

Our project is a privacy-friendly room monitor built with a Raspberry Pi 5, two infrared break-beam sensors, and a magnetic reed switch. The purpose is to detect activity at a doorway and eventually distinguish between human-sized and pet-sized activity without using cameras or microphones.

The two break-beam sensors will be mounted vertically, one above the other. The lower sensor, `beam_lo`, is connected to GPIO17, and the upper sensor, `beam_hi`, is connected to GPIO22. The magnetic reed switch (`door`) uses GPIO27 to detect whether the door is open or closed.

### Architecture Diagram

```text
 PHYSICAL SENSORS
 beam_lo  lower beam   GPIO17 --+
 beam_hi  upper beam   GPIO22 --+
 door     reed switch  GPIO27 --+
                                | level changes, variable rate
                                | (no fixed sampling rate)
 RASPBERRY PI 5                 v
    Linux GPIO subsystem (kernel edge detection + timestamps)
                                |
                                | edge events, variable rate
                                v
                           libgpiod v2
                                |
                                | poll() wakeups, variable rate
                                v
            sensortest.c (one C process, one thread)
                timestamps and formats each event
                                |
                   +------------+------------+
                   | 1 line/event            | 1 line/event
                   v                         v
            Terminal output             events.log
                                             |
                                             | fsync() once per event batch
                                             v
                                      microSD storage
```

### Data Flow

Each sensor's output is either HIGH or LOW, and the Pi sees an edge event whenever that level changes. For the beams, HIGH means clear and LOW means blocked. For the door, HIGH means open and LOW means closed. The two break-beam receivers go through external voltage dividers to keep their signals safe for the Pi's GPIO pins, so their internal GPIO pull resistors are disabled. The reed switch uses an internal pull-up resistor and 50 ms of debouncing.

Our software is written in C17 and uses libgpiod v2 to receive rising and falling edge events from the Linux kernel. The program uses `poll()` to wait for events rather than continuously checking GPIO values. Each edge event has a kernel-generated monotonic timestamp.

Right now, one process and one thread handle all three sensors. The program prints each event to the terminal and appends it to `events.log`. Each line has a timestamp, the sensor name, the GPIO number, and the new state. The log also records the starting state of each sensor and when the program starts and stops.

**Current status:** All three sensors have been tested working together, and their events are being recorded to `events.log`. Human vs. pet classification, process isolation, and automatic recovery are planned but not implemented yet.

## 2. Operating System Mechanisms

Our project will use two operating system mechanisms: interrupt-driven sensor input (Mechanism B) and a multi-process architecture with fault recovery (Mechanism E). These mechanisms will help the room monitor respond to sensor activity and keep running reliably if part of the system fails.

### Mechanism B: Interrupt-Driven Input

Our first mechanism uses Linux GPIO edge detection to monitor the sensors without having to constantly check their states.

The C program uses libgpiod to configure the GPIO pins and receive rising and falling edge events. It uses `poll()` to wait for the kernel to report events, instead of repeatedly reading each sensor in a loop. The kernel also provides timestamps that let us compare when the two break-beam sensors were interrupted.

This mechanism is useful because our sensors only need attention when their states change. Checking them continuously would use more CPU than needed, and checking too slowly could miss short interruptions.

**Status:** Edge-event monitoring is implemented and tested for all three sensors. We still need to write a polling version and compare the two approaches using event latency and CPU usage measurements.

### Mechanism E: Multi-Process Architecture (Planned)

Our second mechanism will separate sensor monitoring and event processing into cooperating Linux processes.

Each sensor will have its own monitoring process that sends events to a central process using inter-process communication (IPC). The central process will combine the sensor data, handle classification, and record the results.

A supervisor process will monitor the sensor processes and restart them if they unexpectedly terminate. This is important because the room monitor is meant to run unattended, and a failure in one sensor process should not stop the entire device.

We will test this by intentionally terminating sensor processes and checking whether the supervisor notices the failure, logs it, and restarts the affected process while the other processes keep running.

**Status:** Not implemented yet. Right now `sensortest.c` handles all three sensors in a single process.

## 3. Failure Modes and Recovery

There are several things that could go wrong with our room monitor. For each one, we want to know how we would recognize it, what the system should do, and what information would be available afterward.

| Failure | How We Detect It | Response | Log Evidence |
|---|---|---|---|
| Break-beam disconnected or misaligned | Unexpected readings, followed by a physical check | Check wiring and alignment; later, flag unreliable readings | Recent sensor events; warnings planned |
| Reed switch stops working | Door reading does not change during testing | Keep monitoring the beams and check the switch | Last door state; warnings planned |
| Missed GPIO events | Planned: check event sequence numbers for gaps | Planned: count missed events and avoid unreliable classifications | Event-loss warnings planned |
| Monitoring program crashes | Program exits unexpectedly; supervisor planned | Restart manually for now; automatic restart later | Missing `stop` message |
| Storage becomes full or fails | `write()` or `fsync()` reports an error | Print an error and exit | Terminal error; log may be incomplete |
| Raspberry Pi loses power | Monitoring stops unexpectedly | Restart after power returns and inspect the log | Missing `stop` message or incomplete line |
| Child process crashes (planned system) | Supervisor notices a stopped process | Restart the affected process | Crash and restart messages planned |

### Current and Planned Recovery

Right now, our program handles basic GPIO and file-writing errors and records when monitoring starts and stops. We've tested what happens when saving data fails, and the program correctly exits with an error.

We still need to implement the supervisor, automatic sensor warnings, and checks for missed events. We'll test these features later to see whether the system actually handles the failures the way we've planned.

## 4. Storage and Data Design

Our room monitor needs to save sensor activity so we can look back at it later, even after the program stops running. To do this, our C program saves sensor events to a file named `events.log` on the Raspberry Pi's microSD card.

### What We Record

Every time one of our sensors changes state, the program records an event in the log file. Each event includes:

- The date and time the event was recorded
- A monotonic timestamp (from the kernel for edge events)
- Which sensor triggered the event (`beam_lo`, `beam_hi`, or `door`)
- The GPIO number, the edge type (rising or falling), and the new state (blocked, clear, open, or closed)

The program also records the starting state of each sensor and adds a message whenever monitoring starts or stops. This helps us separate different monitoring sessions and spot when something might have gone wrong.

### How Data Is Saved

Instead of saving sensor readings at a set interval, our program only records events when something changes. This means the amount of data saved depends on how much activity happens around the doorway.

We use the `write()` system call to add events to the log file and `fsync()` to make sure the data reaches storage. Rather than calling `fsync()` after every single event, we call it once after each batch of events to avoid unnecessary writes to the microSD card. If opening, writing, or syncing the log fails, the program exits with an error instead of running without saving events.

The log is opened in append mode, so new events are added to the end of the file instead of overwriting earlier recordings. We tested this by running our program multiple times and confirming that the earlier events were still there.

### Storage Limits and Privacy

Since our project is meant to protect privacy, we don't collect any images or audio. We only record sensor states and timestamps. However, those timestamps can still reveal activity patterns, such as when someone is home, when they leave, or when they go to sleep, so we still treat the log as sensitive.

Because of this, the log file is created with permissions that only let its owner (the user running the program) read and write it. Currently, the file keeps growing until we manually remove it. For the finished project, we plan to automatically delete older records, with a goal of keeping only the most recent seven days of activity.

### Power Loss and Incomplete Writes

One problem we need to consider is what happens if the Raspberry Pi suddenly loses power while saving an event. Even though we use `fsync()` to reduce the chance of losing data, an unexpected shutdown could still lose recent events or leave an incomplete line in the log.

Because each session has a start and stop message, a session with no stop message could mean the program crashed or the Pi lost power. For now, we would manually inspect the log after a failure. Later, we plan to add a basic recovery check that finds incomplete entries and keeps them from being used when processing sensor activity.

## 5. Constraints and Substitutions

While working on this project, we've had to make a few decisions based on cost, the hardware we have available, and what we can actually build during the semester. Our current setup works, but there are still some limitations.

### Sensor Limitations

Ideally we would have a more accurate way to measure the height of something passing through the doorway. Instead, we are using two inexpensive infrared break-beam sensors mounted at different heights.

This gives us an estimate of whether something is human-sized or pet-sized without needing a camera. However, the sensors cannot measure someone's true height. A person crouching, a pet jumping, or someone carrying an object could cause incorrect readings. We plan to handle situations where the sensors don't give enough information to make a confident classification.

### Wiring and Mounting

Right now all three sensors are connected on a breadboard and have been tested successfully. However, we haven't permanently mounted them around the doorway yet.

One problem we ran into is that the wires included with the sensors aren't long enough to reach across the door frame. We plan to extend the wiring and secure everything in place, then test the sensors again after mounting them to make sure they still work properly.

Another issue was that our break-beam receivers output around 5 V, while the Raspberry Pi's GPIO pins can only safely handle 3.3 V. We used two 100 kΩ resistors as a voltage divider for each receiver to lower the signal voltage, and we tested the circuits with a multimeter before connecting them to the GPIO pins.

### Raspberry Pi and Storage

We decided to use a Raspberry Pi 5 with 2 GB of RAM. It has enough processing power for our project and lets us use Linux features like GPIO edge detection and process management.

One limitation is that we're saving our data to a microSD card, and repeated writes can eventually wear the card down. Power loss is also a risk (see Section 4), and we still need to test how the system behaves after an unexpected shutdown.

### Testing Limitations

We also don't have a large collection of real doorway activity to test our system with yet. So far, we've mainly tested the sensors ourselves by blocking the beams and moving the door magnet.

As we continue, we plan to collect labeled test events using people and pet-sized objects. This should help us figure out how accurately the system can tell the types of activity apart. We'll keep simulated pet-sized tests separate from real-world observations and label them clearly, so our results aren't misleading.

## 6. Evaluation Plan

To figure out how well our room monitor works, we plan to run tests measuring accuracy, performance, and reliability. We want to make sure the sensors detect activity correctly and that the Raspberry Pi can handle everything without using too many resources.

### Sensor Accuracy

Once the sensors are mounted and classification is implemented, we plan to test how accurately the system distinguishes human-sized and pet-sized activity. We will do this by recording at least 30 test events using people and smaller objects passing through the doorway.

For each test, we will record what actually passed through the doorway and compare it with what our program detected. Our goal is at least 90% accuracy for normal test cases. We will also record situations where the system cannot confidently determine what caused the interruption.

### Event Detection and CPU Usage

We also want to compare our interrupt-driven GPIO system with a basic polling version that repeatedly checks the sensors. We will measure how long it takes the program to respond to sensor events and how much CPU each approach uses, running repeated tests both while the Raspberry Pi is idle and while it is handling extra CPU load.

We will record the average, median, 95th percentile, and maximum detection times instead of relying on just one measurement. Our initial goal is to keep the 95th percentile detection delay below 100 ms, even under extra CPU load. We will also measure CPU and memory usage with Linux tools to see whether interrupt-driven monitoring is more efficient than continuously checking the GPIO pins.

### Reliability and Fault Testing

Our final system will need to run for at least 48 hours without anyone checking on it. During this test, we plan to log hourly updates showing whether the processes are running, how much memory they are using, and how many sensor events have been recorded.

We will also intentionally cause problems, such as disconnecting a sensor or terminating a sensor process. Once we implement our supervisor, we want it to detect a crashed process and restart it within 10 seconds. We'll check the logs afterward to see whether the system recognized each problem and recovered correctly.

### Storage Testing

Finally, we want to test whether our event logging saves information correctly. We've already confirmed that events are written to `events.log`, that restarting the program does not overwrite earlier recordings, and that the program exits with an error when saving data fails.

Next, we plan to simulate unexpected shutdowns to see how much data could be lost or left incomplete, and check whether the log is still readable after restarting the program.

## 7. Ownership Map (Proposed)

Since this is a two-person project, we need to divide the work so that we're each responsible for different parts of the system. This is our initial plan based on what's already been completed and what still needs to be built. We may change some responsibilities after working through the remaining tasks together.

### Cole - Hardware, Sensor Monitoring, and Mechanism B

I've been working on the Raspberry Pi setup, wiring the sensors, and writing the C code that monitors the GPIO pins and saves events to a log file. I'll continue to be responsible for these parts of the project, along with Mechanism B, which compares interrupt-driven sensor monitoring against traditional polling.

### Jonah - Process Management, Mechanism E, and Classification (Proposed)

Our proposed plan is for Jonah to take responsibility for the multi-process side of the project. This would involve separating sensor monitoring into different processes, getting those processes to communicate, and creating a supervisor that can restart them if something crashes. He would also take the lead on the logic that uses sensor events to distinguish human-sized and pet-sized activity.

These are proposed responsibilities, not features that have already been completed.

### Shared Responsibilities

We'll both be involved in testing the finished system, reviewing each other's code, updating documentation, and preparing for the final demonstration. We also want to make sure we both understand how the entire project works, even if we're responsible for different parts of the code.

## 8. AI-Use Plan

We're using Claude Code to help us develop and troubleshoot our project, but we still need to understand and test the code ourselves. Our plan is to use it for smaller programming tasks, reviewing code, explaining unfamiliar C and Linux concepts, and helping us figure out problems when something doesn't work.

Rather than asking Claude to build the entire project at once, we've been working on individual features and testing them before moving on. For example, we first tested the sensors separately, then got all three working in one C program, and later added event logging. We also physically tested the wiring and measured the sensor voltages instead of assuming the code or wiring was correct.

We won't use Claude Code or any other AI model inside the actual room monitor. The finished program will be C code that runs locally on the Raspberry Pi, including our own rules for processing sensor events and making classifications. It won't need an AI service or an internet connection to work.

We'll keep our project instructions in `CLAUDE.md`, save our raw Claude Code transcripts, review changes before committing them, and test the code on the Raspberry Pi. We also plan to review each other's work and use a separate Claude session to check for problems we may have missed. Our goal is to use AI as a tool to help with development, not as a replacement for understanding how the project works.
