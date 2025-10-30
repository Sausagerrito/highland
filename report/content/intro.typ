#let project_overview = [
Highland Plastics is a plastic extrusion and compounding company based in Shepherd, Michigan. They research and produce custom polymers and composites for their clients. The company is currently developing a flame-retardant composite called Kelvinite that will be able to safely contain electric vehicle (EV) battery fires. Development of this material entails significant testing to determine if safety standards are met. Specifically, Kelvinite must resist deteriorating at a series of high temperatures for set periods of time. The current industry standard for efficiently testing these materials is a torch and grit (TaG) test. Purchasing the premade equipment to conduct this test is very expensive, so Highland Plastics has fabricated their own testing equipment. Currently, the test is completely manual and lacks features that would allow for automatic data collection and analysis, better reproducibility, and increased safety. 

The scope of this project encompasses everything required to fully automate Highland Plastics’ torch and grit test and add any required safety features. The project must also comply with UL 2596 standards for TaG testing, the only exception being that Highland Plastics has chosen not to use grit to improve test reproducibility. Throughout the course of this project, a PID-controlled air and gas system will be designed and fabricated to control the torch flame, and computer-based software will be developed to control testing parameters, collect relevant data, and display or export data trends. A microcontroller will also be used to control all actuators and compile raw data inputs.

]

#let customers = [
The primary customer for this project is Highland Plastics, as their specifications and needs drive the design of the project. After the completion of this project, Highland Plastics will take sole ownership of the final product.

The secondary customers involved include Highland Plastic’s clients, who may utilize the testing data. Central Michigan University’s role as another secondary customer involves supporting the project with resources, mentorship, and academic insight. The student team are additional secondary customers as well, as the group will benefit from the hands-on experience and professional development gained from the project.

]

#let constraints = [
The table that the testing apparatus is built upon must be able to be lifted with a forklift and allow for modularity while remaining structurally sound. The testing apparatus must be highly modifiable for different testing setups. 

The system will be powered by a standard AC outlet (120V). The current assumption is that a centralized power system will be used for the whole system, creating multiple DC power sources for the system to safely use. With a centralized power system, a safety trip can be placed at the start  to shut the system down in the event of a power surge, and fuses along the way will shut the entire system down if a component stops working (Type TBD). Since the system is designed to be moved via forklift, all the electrical equipment will be organized and mounted properly while also being accessible for easy quick connect/disconnect. An emergency stop feature (TBD) will be placed on the system as well. Since the project involves high temperatures and open flames, this feature is essential for employees' safety. Additionally, if a power outage occurs, a backup battery system to preserve data on RAM for upload could be implemented for added functionality.

The project requires a microcontroller that’s powerful enough to handle real-time data processing, has many I/O options, and has plenty of documentation. Additionally, any code written for the project must be well documented and easy to debug. The test apparatus will be maintained solely by the current staff, many of whom have no experience with microcontrollers or writing code, so creating code that will be easy to debug will be one of the biggest constraints the student team will have to overcome. 

]

#let stakeholders = [
Stakeholders for the project include Highland Plastics, who are the end users of the testing equipment. Central Michigan University is another stakeholder who provides guidance and resources for the project. Another stakeholder is faculty advisor Dr. Mohammed Awad, who supports the student team. The student team is also a stakeholder for the project, as the team directly influenced the design and implementation of resources into the project.
]

#let project_goals = [
This project involves a multitude of goals that are essential to the success of the initiative.  The overall goal of this project is to develop an automated test bench that accelerates product development and testing for Highland Plastics by optimizing a flame testing process with a reproducible torch flame. The table must be robust, flame resistant, and include safety features to eliminate any chances of harm. Obtaining accuracy among data collection for each test is another crucial goal for the success of the project. This will be achievable by utilizing a microcontroller and a PID loop to collect and analyze data. By achieving accurate and precise data, the table will become a vital tool for current and future developments within Highland Plastics. Adhering to all design specifications provided by Highland Plastics as well as ensuring that the final product is visually appealing for marketing purposes is also adamant.
]

#let customer_needs = [
For this project to be a success, Highland Plastics needs a few key requirements to be met. While some of these needs are small in scope and don’t have any impact on the actual testing data, they are still important because of the effect they would have on public perception of the company. For instance, to appear more professional, Highland Plastics wants a new setup to replace their current one that looks professional, has a stopwatch that displays the test time, and will look visually appealing for tests to be filmed and posted online. Additionally, Highland Plastics requires the new setup to give a positive impression of their work area to attract new customers and garner better public opinion. As for the technical aspects of Highland Plastics’ needs, the setup must have cold and hot side temperatures prominently displayed, must transmit test data to a computer for analysis, and be easy to fix if any issues were to arise in the future.
]

#let design_targets = [
Outside of Highland Plastics’ needs, there are a few target specifications that must be met to comply with UL-2596 and meet the needs of Highland Plastics. Arguably, the most important key specification is that the burner must reliably achieve and hold a 1200 °C flame (±50 °C). Since the tests Highland Plastics wants to run completely revolve around the temperature being applied to the composite they are testing, having a flame that can produce a consistent temperature is crucial. Besides having a reliable flame temperature, the setup also needs to have a sample holder that can fit a 200x200 mm piece of composite; this will give Highland Plastics the freedom to experiment with different composites while not compromising on the test quality. The last key specification is that the software the team will write must receive data from a microcontroller and output the test time and hot and cold side temperatures. Moreover, this data must then be sent to a computer for analysis to be done in the future.
]

