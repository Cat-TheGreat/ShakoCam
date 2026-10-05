 # ShakoCam
An athletic headband with a camera. Like a GoPro, but sleeker, smaller, and much more comfortable and affordable. Designed to take POV videos during marching band performances, which is where the name comes from. My band director once said that you never really know what actually happened at the last competition of the season because the only evidence is memories, and memories tend to shift. That line was most of the inspiration for this project, and I hope it can solve the eternal dilemma.
 ## How It Works
A 1000mAh battery powers a Xiao ESP32S3, chosen for its small size but big processing power. The ESP32S3 runs its own WiFi network, which the user can connect to on their phone. They then open up a web app on their browser, which allows the user to Start/Stop recording, view SD card storage, and view recorded videos in the browser, as well as download them as MP4s to their phone. The video is recorded through an OV3660 with a 120 degree lens to capture the user's field of view. The battery can be recharged using the ESP32S3's USB. 
 ## How To Build It
 ### 1. Prepping Electronics
Follow this wiring diagram to solder the 26 AWG wire from the LiPo battery to the ESP32S3
<img width="830" height="720" alt="Wires_Connections V1" src="https://github.com/user-attachments/assets/f2e4592e-a281-4a1c-92e6-0c5d3c374c8c" />

Locate the camera module that comes with the ESP32S3, flip the black latch open, and remove it. Insert the longer OV3660 module.
Upload ShakoCam code onto the ESP32S3, make sure you can access the web app with your phone by connecting to ShakoCam WiFi and opening http://192.168.4.1/
 ### 2. Enclose the Electronics
3D print the battery, camera module, and ESP32S3 cases. Onshape: https://cad.onshape.com/documents/f96a4d49e3e12c14028028a5/w/6497ad0440bc11f84819475d/e/ab09dfae09777d82fe9ced0d?renderMode=0&uiState=6ac3121affeee4e9a2523c6c
Place Velcro as shown
<img width="757" height="768" alt="Velcro V1" src="https://github.com/user-attachments/assets/4f3743d6-269c-4a48-a97d-f4d699a2f31f" />

Carefully put components in their respective enclosure and make sure all pieces are secured
 ### 3. Mounting onto the headband
Cut the outer layer of the inside of the headband to get access to the inner layers
Sew matching velcro pieces onto the inside of the headband 
Attach the ESP32S3 and battery cases
Cut a hole where you want the camera lens to be, then put the camera module enclosure through and secure loose threads with adhesive
Enjoy your ShakoCam! Final product should look like this: <img width="675" height="566" alt="CAD V1 Bird&#39;s View" src="https://github.com/user-attachments/assets/7b934321-e88b-488f-bfbf-5cb38da58e6c" />
<img width="724" height="647" alt="CAD V1 Back Tilt" src="https://github.com/user-attachments/assets/de411ea3-64dc-4b22-a7d2-fd53d266e7e3" />
<img width="849" height="565" alt="CAD V1 Front Tilt" src="https://github.com/user-attachments/assets/e73abefb-8365-4eac-8c0d-091d50a74a5b" />

