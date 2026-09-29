from gpiozero import Button
from time import sleep

#INPUT on GPIO27 (physical pin 13).
# The switch's other wire connects to ground (physicall pin 14).
#pull_up=True enables the pi's internal pull-up resistor.
reed = Button(27, pull_up=True, bounce_time=0.05)

previous_state = None

print("Reed switch test running.")
print("Move the magnet toward and away from the sensor.")
print("Press ctrl+c to stop.")

try: 
    while True:
        door_closed = reed.is_pressed

        #print at startup and whenever the state changes.
        if door_closed != previous_state:
            if door_closed:
                print("Door CLOSED - magnet nearby")
            else:
                print("Door OPEN - magnet away")

            previous_state = door_closed

        sleep(0.05)

except KeyboardInterrupt:
    print("\nTest stopped.")

finally:
    reed.close()