# About This Project
This is a smart farming project for my final university project. The idea of the project is to make automated fertilizer delivery system for hydroponic plant. Lettuce is used as subject in this project.
# Algorithm
The system works as following diagram.
<img width="669" height="554" alt="algoritma 3 ttu_revisi_Eng" src="https://github.com/user-attachments/assets/60a8a4ba-1382-44a8-a093-de528b255cf0" /><br/>
There are 3 states that applies to this system:
### Plant's Age Calculation
The calculation is conducted to define the required concentartion of plant's nutrition.
### Required Nutrition's Volume and Pump's Operating Time Calculation
Based on the age calculation, the system is able to calculate the required volume of A and B nutrition. The operating time of A and B pump also calculated in this state. Dilution method is used for the calculation. The equation is shown below:<br/>
<img width="275" height="76" alt="image" src="https://github.com/user-attachments/assets/e49467a7-caf7-481c-ab6c-694be96b48c5" /><br/>
Where:
* V1      = Requiered A or B volume
* v2      = Designated overall volume after mixing
* Ctarget = Nutrition concentartion target based on age calculation
* CNow    = Current nutrition concentration reading
* Ca      = Concentartion of A nutrition
* Cb      = Concentartion of B nutrition<br/>
When the V1 is already known, the system can continue to calculate the operating time of A and B nutrition's pump. The equation of time calculation is shown below:<br/>
<img width="108" height="81" alt="image" src="https://github.com/user-attachments/assets/74f57970-6b5a-4851-a809-a1d57f8170f2" /><br/>
Where:
* t  = Time of pump operation
* V1 = Requiered A or B volume
* Q  = Liquid flow rate of the pump (can be found in datasheet)
### Aditional Pump Operation
When the result of calculation above has been conducted and the sensor's reading isn't matching the designated concentartion, the system operate pump A and B for one second. Finally if the result is enough as targeted, the system enter the monitoring state.
