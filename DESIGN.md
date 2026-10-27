CS370 Term Project - Design Document

1. System Architecture

Our project serves as a privacy room monitor built using a Raspberry Pi 5, two infrared break-beam sensors, and a magnetic reed switch. The purpose is to detect activity at a doorway and eventually distinguish between human-sized and pet-sized activity without using cameras or microphones.

The two break-beam sensors will be mounted vertically, one above the other. The lower sensor named beam_lo will be connected to GPIO17, and the upper sensor, beam_hi will be connected to GPIO22. The magnetic reed switch (door) uses GPIO27 to detect whether the door is open or closed.

Architecture Diagram:

PHYSICAL SENSORS
                       Raspberry Pi 5
  Lower beam (GPIO17) ----\
                           \
  Upper beam (GPIO22) ------> Linux GPIO subsystem
                           /    (kernel edge detection
  Reed switch (GPIO27) ---/      and timestamps)
    Variable signal               |
    changes; no fixed             | Edge events as detected
    sampling rate                 | (variable rate)
                                  v
                          libgpiod v2
                                  |
                                  | Event-driven wakeups
                                  | (variable rate)
                                  v
                          sensortest.c
                      (one C process/thread)
                                  |
                      Timestamp and format
                          sensor events
                                  |
                       +----------+----------+
                       |                     |
                       v                     v
                 Terminal output          events.log
                 (1 line/event)          (1 line/event)
                                             |
                                             | fsync after
                                             | event batches
                                             v
                                      microSD storage

Data Flow: 

The sensors send HIGH or LOW electrical signals to the Raspberry Pi whenever their states change. The two break-beam sensors use external voltage dividers to keep their signals safe for the Pi's GPIO pins. Their internal GPIO pull resistors are disabled, while the reed switch uses an internal pull up resistor and 50 ms of debouncing.

Our software is written in C17 and uses libgpiod to receive rising and falling edge events from the Linux kernel. The program uses poll() to wait for events rather than continously checking GPIO values. Each event has a kernel generatated timestamp.

As of now, one process and one thread handle all three sensors, the program prints each event to the terminal and appends it to events.log file. The log contains timestamps along with sensor names, GPIO numbers, and state changes. It also records program startup and shutdown. 

Sensor events occur at varabile rate depending on the physical activity, rather than ata fixed sampling frequency. The logging system uses fsync() after event batches to synch recorded data to storage.

The current stats: all three sensors have been tested working together, and their events are successfully getting recorded to events.log. Human versus pet clasification has not been implemented yet, process isolation and automatic recovery are planned but also not implemented yet.


2. Operating System Mechanisms

Our project will use two operating system mechanisms: interrupt-driven sensor input (Mechanism B) and a multi-process architecture with fault recovery (Mechanism E). These mechanisms will help the room monitor respond to sensor activity and continue to operate reilaibly if part of the system fails.

Mechanism B: Interrupt-driven Input

Our first mechanism uses Linux GPIO edge detection to monitor the sensors without having to constantly check their states.

The C program uses libgpiod to configure the GPIO pins and receive rising and falling edge events. It uses poll() to wait for the kernel to report events, instead of repeatedly reading each sensor in a loop. the kernel also provides timestamps that allow us to compare when the two break-beam sensors were interrupted.

This mechanism is useful becuase our sensors only need attention when their states change. checking them continuously them would use too much processing resources then needed, and checking too slowly could miss short interruptions.

We have already implemented and tested GPIO edge-event monitoring for all three sensors. Later we will implement a polling version and compare the two approaches using event latency and cpu utilization measurements.


MEachnism E: Multi-Process Architecture

Our second planned mechanism will seperate sensor monitoring and event processing into cooperating Linux processes.

What this means is that each sensor will have its own monitoring process that sends events to a central process using inter-process communication (IPC). The main process will combine the sensor data, handle classification, and record the results.

We will have a supervisor process that will monitor the sensor processes and restart them if they unexpectedly terminate. This is important because the room monitor is intended to operate unattended, and a failure in one sensor process should not stop the entire device.

We will test this by intentionally teriminating sensor processes and checking wither the supervisor sees the failure, logs it, then restart the affected process while the remaining processes continue operating.

Current implementation status: Our existing sensortest.c Program monitors all three sensors in one process and saves events to persistent storage. MEchanism B's event driven input is operational, but its polling comparision has not been completed. Mechanism E has not been impemented yet and is currently part of our planned architecture.

3. Failure Modes and Recovery

There are a few things that could go wrong with our room monitor, whether its a sensor not working correctly or a problem with the Raspberry Pi itself. We want to make sure that the system can recognize these problems and handle them without recording incorrect information. below are some possible failure points and how we plan to deal with them

1. Break-Beam Sensor Failure

One of the break-beam sensors could become unplugged or get moved out of alignment, causing it to give incorrect readings.

- Detection: If a sensor stays blocked or clear when it shouldnt, we would need to check the wiring and alignment.
- Response: We plan to have the program flag unusual readings and avoid making a classification if a sensor isnt working correctly.
- Logged Evidence: The sensor's recent readings and any warnings we add later. 

2. Magnetic Reed Switch Failure.

The magnetic reed switch could come loose or stop detecting when the door opens and closes.

- Detection: We can test the switch by moving the magnet and checking whether the GPIO reading changes.
- Response: The break-beam sensors should still be able to run, but we wouldnt want to rely on the door reading until the problem is fixed.
- Logged Evidence: The last recorded door state and any future error messages.

3. Missed Sensor Events

If the Raspberry Pi gets overloaded, it's possible that some sensor events could be missed.

- Detection: We plan to use event sequence numbers to check whether any events were dropped. 
- Response: Keep track of missing events so we know when our readings might not be accurate
- Logged Evidence: A count of missed events and any related warnings.

4. Program Crash

Our C program could crash or stop running unexpectedly. 

- Detection: We plan to add a supervisor process that can tell when another process has stopped.
- Response: The supervisor would restart the process so the monitor can continue running.
- Logged Evidence: The event log might be missing a normal shutdown message. Later, we also want to record when a process crashes and restarts.

5. Storage Failure

The Raspberry Pi could run out of starge space, or something could go wrong while writing the microSD card.

- Detection: Our C progarm already checks for errors when writing and saving using write() and fsync().
- Response: If saving fails, the program prints an error and stops instead of continuing without recording events.
- Logged Evidence: An error message in the terminal and possible an incomplete event log.

6. Power Failure

If the Raspberry Pi loses power, hte program will stop running and could lose events that havent been saved yet.

- Detection: We can check whether the last monitoring session has a normal shutdown message. 
- Response: Once power is restored, we would restart the program and check the log for missing or incomplete data.
- Logged Evidence: A missing shutdown message or an incomplete final entry.

Current and PLanned Recovery

Right now, our progarm can detect basic GPIO errors and problems with saving data. We have also tested what happens when the program can't write to a file, and it correctly exits with an error. The program aslo records when it starts and stops, which should helps us identify unexpected shutdowns. 

We will still need to implement some of the more advanced features, like automatically restarting crashed processes, checking for missed events, and identifying sensor problems. These are things we plan to work on as we continue developing and testing the project.

4. Storage and Data Design

Our room monitor needs a way to save sensor activity so we can look back at it later even after the program is not running. To do this, we added a logging system to our C program that saves sensor events to a file named events.log on the Raspberry Pi's microSD card.

What we record: 

Every time one of our sensors changes state, the program records an event in the log file. Each event includes: 
    - The date and time the event was recorded
    - A monotonic timestamp provided by the Linux kernel.
    - Which sensor triggered the event (beam_lo, beam_hi, or door).
    - The GPIO pin number and whether the sensor was blocked, clear, open, or closed.

The program aslo records the starting state of each sensor and adds a message whenever monitoring starts or stops. This helps us separate different monitoring sessions and identify when something might have gone wrong.

How Data Is Saved

Instead of saving sensor readings at a set interval, our program only records events when something changes. This means the amount of data being saved depends on how much activity happens around the doorway.

We use the Linux write() system call to add events to the log file and fsync() to make sure the data is synchronized to storage. Rather than calling fsync() after every individual event, we call it after batches of events to avoid unnecessary writes to the microSD card.

We also use append mode which means new events are added to the end of the file instad of overwriting previous recordings. We tested this by running our program multiple times and confirming that the earlier events were still there.

Storage Limits and Privacy

Since our project is meant to protect privacy, we dont collect any images, audio, or personal information. We only record sensor states adn timestamps

The log file is created with permissions that only allow the Raspberry Pi user to read and write it. Currently, the file continues growing until we manually remove it. For the finished project, we plan to add a way to automatically delete older records, with a goal of keeping only the most recent seven days of activity.

Power Losss and Incomplete Writes

One problem we need to look at and consider is what will happen if the Raspberry Pi suddenly loses power while saving an event. though we use fsync() to reduce the chance of losing data, a shutdown unexpectadely could result in losing recent events or leave an incomplete line in the log.

Our current program records a start and stop message for each session. If a session doesn't have a stop message, that could mean the program crashed or the Pi lost power.

For now, we would manually inspect the log after a failure. Later, we plan to add a basic recovery check that can identify incomplete entries and prevent them from being used when processing sensor activity.


5. Constraints and Substitutions

While working on this project, weve had to make a few dicisions based on how much we were willing to spend, the hardware we have available to us, and what we could actually build during the semester. Our current setup works, but there are still some places where limitations exist.

Sensor Limitations

It would be nice to be able to have a more accurate measurement for height of something passing through the doorway. Instead we are using two inexpensive infrared break-beam sensors mounted at different heights.

This gives us an estimate whether something is human sized or pet sized without needing a camera. Howver, the sensors cannot measure someones true height. Things like a person crouching, a pet jumping, or someone carrying an object could cause incorrect readings. We plan to account for situations wehere the sensors dont provide enough information to make a confident classification.

Wiring and Mounting

Right now all three sensors are connected to a breadboard and have been tested succesffully. However, we havent permanently mounted them around the doorway yet.

One problem we ran into is that the wires included with the sensors arent long enough to reach across the door frame. we plan to extend the wiring and secure everything in place. We will aslo need to teset the sensors again after mounting them to make sure they still work properly.

Another issue that we ran into was that our break-beam receivers output around 5V, while the Raspberry Pi's GPIO pins can only safely handle 3.3V. We used two 100k resistors as a voltage divider for each receiver to lower the signal voltage. We tested the circuits with a multimeter before connecting the signals to the GPIO pins.

Raspberry Pi and Storage

We decided to use a Raspberry Pi 5 with 2GB of RAM. It has enough processing power for our project and allows us to use Linux features like GPIO edge detection and process management. 

One limitation is that we're saving our data to a microSD card. Sudden power loss could interrupt a write, and repeated write can eventually wear down the card. we're using fsync() to help protect our recorded events, but we still need to test how the system behaves after an unexpected shutdown. 

Testing Limitations

We also dont have a large collection of real doorway activity to test our system with yet. So far, weve mainly tested the sensors ourselves by blocking the beams and moving the door magnet.

As we continue working, we plan to collect labeled test events using people and pet sized objects. This shoud help us figure out how accurate the system is able to differentiate between types of activity. Well keep simulated pet sized tests separate from actual real world observations so our results arent misleading. 

6. Evaluation Plan

To figure out how well our room monitor works, we plan to run several tests measuring accuracy, performance, and reliability. We want to make sure the sensors detect activity correctly and that the Raspberry Pi can handle everything without using too many resources.

Sensor Accuracy

Once the sensors are mounted, we plan to test how accurate the system is able to distinguish human sized and pet sized activity. we will do this by recording at least 30 test events using people and smaller objbects passing through the doorway.

For each test, we will record what actually passed through the doorway and compare it with what our program detected. Our goal is at leasat 90% accuracy for normal test cases. We will also record situations wherre the system cannot confidently determine what cause the interruption. 

Since we havent implemented classification yet, these tests will take place after that part of the program is done.

Event Detection and CPU Usage
