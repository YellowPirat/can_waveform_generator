
# Install
- cloning repository
  - git clone --recursive-submodules [path]

- Update the submodules from dbcppp

  - git submodule update --init --recursive


- conan install . --output-folder=build --build=missing
- cd build 
- cmake .. -DCMAKE_TOOLCHAIN_FILE=build/Release/generators//conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
- cmake --build . -v

# Waveform

## Base-Waveform 
To start generating can-messages, a base-config file is required. To add a new CAN-Bus to the BaseConfigNode you have to create a field. In the field, the name of the CAN-Bus, the DBC-file-name and the customWaveFileName are required. 

## Waveform file
A waveform file consists of a update Duration part and a waveform part. The waveform part consists of Sets. A Set has a type. If the type is single, a single CAN-Message and its CAN-Message-Signals can be described independently. If the type is multi, all the mentioned CAN-Messages in the parts have the same waveform behavior.