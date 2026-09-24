# Problem memo -- <team awesome> (<Cole Rechsteiner>, <Jonah Tomarchio>)



## The user
Our initial target user is a college student living in an apartment who wants to know when someone enters or leaves whie they are away,
without using a camera. The system is designed so that it could later be used in other homes, dorms, offices, or rooms with a single monitored entrance.


## The problem
When someone is away from their room or apartment, they may want to know whether someone entered, when it happened, and whether it was a person or possibly an animal or pet. A normal door sensor can tell that the door opened, but it cannot explain what actually happened. Security cameras can provide more information, however they are more expensive, create privacy concerns, and require reviewing video footage.


## Why a device
This problem requires something that is physically installed at the doorway and stays active even when the user is not home. A phone app by itself cannot detect someone crossing a doorway because the phone is not permanently located at the entrance. The Raspberry Pi device can remain powered on and continously monitor the sensors without requiring the user to interact with it.


## The sensors
The system will use a magnetic door sensor and two infared break-beam sensors. The door sensor will detect when the door opens or closes. The two break-beam sensors will be positioned in the door way at different heights. By looking at if either one or both beam is broken, the system can distinguish between something that is human sized, or pet sized entering the room. The door sensor provides additional confirmation that the event is associated with an actual door opening instead of someone simply walking near the sensors, and also let the user know the open/closed state of the door. 


## The mechanisms
Our first planned mechanism is interrupt-driven input (Mechanism B). The door sensor and break-beam sensors can generate GPIO events when their states change, allowing the system to react to events without constantly polling each sensor.
Our second planned mechanisim is a multi-process architecture with supervision (Mechanism E). Sensor handling can be seperated into processes that report events to a central process. A supervisor can detect if one sensor process crashes or stops responding and restart it while allowing the rest of the system to continue operating.


## The risk
The largest risk is reliably determining the size of the object moving into the room. People may move slowly, stop in the doorway, trigger both beams almost simultaneously, or partially block one sensor. The sensors will also need to stay aligned. If the timing and event logic are not reliable, the system could incorrectly classify someone as a pet or vise versa.
