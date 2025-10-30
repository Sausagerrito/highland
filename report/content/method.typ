#let design1 = [
  This design concept involves the use of a modular table design that utilizes holes for mounting testing equipment. This is done to future-proof the testing table for evolving testing standards. As testing standards may change regarding physical requirements, the table can be adjusted to fulfill these needs. This testing table also has two mounts for air and methane fuel gas canisters. This is significant because mounting the tanks to the table allows for easy maneuverability of the table. The gas tanks are attached to a regulator, flow meter, control valve, and a torch specified by UL2596. These components will all be mounted to the table as well. The fixture holding the testing material will sit attached to a sliding rail system for maneuverability across from the torch. To allow the torch flame to fully heat before burning the test piece, an electronically enabled stopper will sit between the test piece and the torch, automatically moving out of the way as the torch flame reaches its desired temperature. Thermocouples are attached to a computer system, stopper, and the testing piece will allow for the stopper to move, and data from the test to be automatically recorded to a data logging system on the computer.
]

#let design2 = [
  The traditional method of reaching a target temperature with a torch involves monitoring the flame temperature with a thermocouple and adjusting the heat input manually or through a PID loop. This approach requires a premium thermocouple rated above 1200 °C and a mechanized system to move components, both of which add cost and complexity. These pain points can be alleviated by using predictive control in the PID loop based on gas flow. The amounts of oxygen and methane entering the torch can be used to estimate the heat output of the reaction, providing a reliable temperature estimate. With this method, no mechanical movement is necessary, since the system releases only the amount of gas required to reach the target temperature quickly, imparting negligible heat rise to the test piece.
]

#let design3 = [
  This design concept uses every aspect of the UL 2596 TaG test apart from the grit system. These aspects include a barrier between the torch and sample that would be automatically removed when the torch has reached a desired temperature, allowing for nearly instantaneous contact of the sample with a fully heated flame. This would allow the test to be more accurate to an actual thermal runaway event, where high temperatures could build very quickly rather than over several minutes. An automated system involving some type of linear actuation will be required to incorporate this concept into the final build. Alternatively, a manual version of this concept could be used for cost savings but would need to keep the user safe from the burner flame.

2-3 type K thermocouples rated to ~1300°C would be used in this design as well. This would allow the system to detect when the burner has reached the desired temperature and automatically move the preheat barrier out of the way. Additional thermocouples would be placed on the hot and cold sides of the sample for data logging and to feed data into the PID control loop to maintain a steady flame temperature. This concept also utilizes adjustable modules that would allow for greater test adaptability and an automated data collection system.
]

#let concept = [
Based on the given target specifications, which call for a burner to reliably achieve and hold a 1200°C flame, have a sample holder that can fit a 200x200 mm piece of composite, and software that can receive data in real-time from a microcontroller, the concept that best fits the target specifications is Design Concept 3. By using a PID loop, preheat barrier, and thermocouples, the target specifications can reliably be achieved while also providing incredibly accurate data due to the use of thermocouples. Moreover, by utilizing a preheat barrier, it can be assured that the torch is only being used on the test composite when a target temperature has been reached. The instant exposure to a high temperature instead of a slowly rising temperature will also more closely resemble the effects of thermal runaway . Even though this design is more expensive than the other design concepts, the additional cost will result in quality improvements to the data collected.
]


#let prototype = [
  The proposed prototype we decided on was concept 3, involving the modular table, data collection, PID Loop, preheat barrier, and thermocouples.
]

#let timeline = [
  
] 

#let work = [
  The project is segmented into technical tasks with respective duties for each team member in preparation for development. Matthew Coeling, Ethan Grentz, and Robert Alm will be responsible for the PID Loop and System Wiring, collaboratively designing, integrating, and constructing the control system with emphasis on functionality and wire organization. Connor Bendele will oversee microcontroller setup and hardware integration, including programming, hardware compatibility verification, and coordinating with Robert to enable software integration. Robert Alm will also handle software development, tasked with creating the desktop application, version control, and system testing and validation guidance. Sam Hass and Elijah Dawson will be the project leads for Fuel System and Fixture Design. Sam will manage CAD modeling, physical assembly, and coordination with Highland Plastics for the team. Elijah will be involved in design and assembly tasks and perform flow rate calculations.
]
