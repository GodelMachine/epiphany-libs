################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/CoreId.cpp \
../src/GdbServer.cpp \
../src/MemRange.cpp \
../src/MpHash.cpp \
../src/ProcessInfo.cpp \
../src/RspConnection.cpp \
../src/RspPacket.cpp \
../src/Utils.cpp \
../src/main.cpp \
../src/ServerInfo.cpp \
../src/TargetControl.cpp \
../src/TargetControlHardware.cpp 

OBJS += \
./src/CoreId.o \
./src/GdbServer.o \
./src/MemRange.o \
./src/MpHash.o \
./src/ProcessInfo.o \
./src/RspConnection.o \
./src/RspPacket.o \
./src/Utils.o \
./src/main.o \
./src/ServerInfo.o \
./src/TargetControl.o \
./src/TargetControlHardware.o 

CPP_DEPS += \
./src/CoreId.d \
./src/GdbServer.d \
./src/MemRange.d \
./src/MpHash.d \
./src/ProcessInfo.d \
./src/RspConnection.d \
./src/RspPacket.d \
./src/Utils.d \
./src/main.d \
./src/ServerInfo.d \
./src/TargetControl.d \
./src/TargetControlHardware.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -I../../ -O0 -g3 -Wall -c  -MMD   -DDUMPVCD=1 -DDV_FAKECLK -DDV_FAKELAT -DDV_FAKEIO -DVL_PRINTF=printf -DVM_TRACE=1  -DSYSTEMPERL -DUTIL_PRINTF=sp_log_printf -Wno-deprecated -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


