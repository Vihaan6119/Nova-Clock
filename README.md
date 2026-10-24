# Nova Clock

Nova Clock is a desktop alarm clock which uses a Seeed Studio XIAO ESP32-C3. It has a 2.25-inch TFT display, four mechanical keyboard switches, a piezo buzzer and all of these will be on a custom PCB with an enclosure/case where all of these will sit in. The PCB, enclosure, and firmware were all designed as part of the same project so the electronics and the case fit together as one system.

## Why I made it

I wanted to build a project that was more than just a basic clock. I wanted to learn how a real hardware product comes together, from designing the PCB and enclosure to writing the firmware and planning how the final device would actually be used.
I also wanted the clock to be something I could keep improving later, especially the display styles, alarms, sounds, and the way it can be used from different device.

## Project

The final design uses a custom PCB with the XIAO ESP32-C3 as the main controller for the clock. The four switches are used for local controls like if the alarm is ringing, then the we can use the switches to turn the alarm off. The TFT display is used for the clock and other information. The buzzer make the sound for the alarms and other things which we like to add.

The enclosure was designed around the PCB so the board can be mounted securely. There is a cutout on the back of the enclosure so that we can easily connect a power/data cable. The display is held in place with a separate retainer so it can be removed without having to glue it into the case.

## How I planned the firmware

The firmware is included in the repository as a prototype for the hardware design. The main idea is:

- show a short Nova Clock startup animation
- play a startup beep
- display the current time
- use the buttons for local clock and alarm controls
- store alarm settings on the ESP32-C3
- connect the clock to Wi-Fi
- provide a local web interface for changing alarm and display settings
- support different display styles and alarm sound patterns (if the buzzer supports it)

The current firmware is just the PROTOTYPE and will be way different from the current version as after i get all the parts for this build, i will configure them and then properly test and see how eveything is working.

## The enclosure talk

I have started designing the case in openSAD as i thought that this will be the easiest from all the other softwares out there, and it proved me wrong. As i was making the enclosure, it was getting harder to get the precise geomatrical measurements into it, and i also could not check if the PCB all the components would fit in it. 

Then I switched to tinkercad. there i imported PCB, and made the base and the lid. But here too, i did not understand how to make the holes on the shapes. 

So then again i switched to Onshape (here i finished making it). I first imported the prototype case i build in tinker cad and tried to build on it. But failed. Then i watched a bunch of tutorials from which, i learn how onshape work. The i started making the PCB enclosure/case. Then after that was done, i imported the PCB .step file. to see if it would fit, and it didn't fit. 

It took me a lot of time correcting it and making it properly fit the PCB and all the parts. 
Then I had assembled the full thing. i mean, got all the 4 thing (the base, the top lid, the retainer and the PCB with all the components) and assembled it showing how it will be looking after i have all the components with me.

Then i exported all the .step file and uploaded it to this repo. 

## Important thing about viewing the the full assemble .step file

Unfortunaltly, you have to do a little more work in order to see the full assemble 3D model. 

I have used Onshape in the instruction.

* **uploading:** Import the file **Nova Clock Full Assembly.step** from the folder **CAD** (of my repo).
* It will take aorund 1-2 mins for it to import and load all the file into it.
* **Selecting the file form the menu:** After the import has finished, there will be load and loads of file. From there you have to open 1 file named **Nova Clock-V2 1**.

![alt text](<assets/Screenshot 2026-10-24 124452.png>)
  
## Project images

### Case fit

![alt text](<assets/case_fit.png>)

### PCB

![alt text](<assets/pcb_layout.png>)

### Schematic / wiring

![alt text](<assets/schematic.png>)

### Project assembly

![alt text](<assets/Screenshot 2026-10-24 124929.png>)

The display is connected to the PCB using the 8-pin header and jumper wires included in the kit.

## Files

The repository has the KiCad project and PCB source files, the CAD source and STEP assembly, the firmware, manufacturing files, and the project images.

- `PCB/` - KiCad project, schematic, PCB layout, and manufacturing files
- `CAD/` - enclosure source files and final STEP assembly
- `Firmware/` - Nova Clock firmware
- `Production/` - production/export files
- `assets/` - screenshots and project images


## Bill of Materials

| Component | Quantity Used | Quantity in Kit | Description | Link |
|---|---:|---:|---|---|
| Seeed Studio XIAO ESP32-C3 | 1 | 1 | Main microcontroller | BLARE Kit |
| MX-style keyboard switch | 4 | 12 | Mechanical switches | BLARE Kit |
| White blank DSA keycap | 4 | 12 | Keycaps for the switches | BLARE Kit |
| 1N4148 through-hole diode | 4 | 12 | Switch matrix diodes | BLARE Kit|
| 2.25-inch TFT display | 1 | 1 | Main clock display | BLARE Kit |
| 3.3 V piezo buzzer | 1 | 1 | Startup and alarm sound | BLARE Kit |
| 2.54 mm 8-pin male header | 1 | 1 | Display connector | BLARE Kit |
| 20 cm female-female jumper wire | 8 | 8 | Display wiring | BLARE Kit |
| M3x5x4 heat-set insert | 8 | 8 | Case, PCB, and display mounting | BLARE Kit |
| M3x8 screw | 4 | 4 | PCB/display-retainer mounting | BLARE Kit |
| M3x16 screw | 4 | 4 | Main enclosure screws | BLARE Kit |
| Custom 2-layer PCB | 1 | 1 | PCB designed in KiCad | [Project PCB](PCB/)  from JLCPCB. Price: $11.1-13.1|
| 3D-printed enclosure | 1 | 1 | Custom Nova Clock case | [Project CAD](CAD/) from printing Legion|
