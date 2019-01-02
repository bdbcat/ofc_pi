

#Toolchain and options definition file for OPenCPN Android build


#  Locations of the cross-compiler tools

# this one is important for bundled builds, but must be commented away for standalone builds
# otherwise you get:
# "ADD_LIBRARY for library oesenc_pi is used with the SHARED option, but the target platform supports only STATIC libraries"

#SET(CMAKE_SYSTEM_NAME Generic)


#this one not so much
SET(CMAKE_SYSTEM_VERSION 1)

# specify the cross compiler
SET(CMAKE_C_COMPILER   /home/dsr/Projects/android-ndk/ndk-toolchain/bin/arm-linux-androideabi-gcc)
SET(CMAKE_CXX_COMPILER   /home/dsr/Projects/android-ndk/ndk-toolchain/bin/arm-linux-androideabi-g++)


SET(TARGET_SUPPORTS_SHARED_LIBS TRUE)

#Location of the root of the Android NDK
SET(NDK_Base /home/dsr/Projects/android-ndk/android-ndk-r10 )