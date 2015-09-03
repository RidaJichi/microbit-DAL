#include "MicroBit.h"

/**
  * Constructor. 
  * Create a compass representation with the given ID.
  * @param id the event ID of the compass object.
  * @param address the default address for the compass register
  *
  * Example:
  * @code
  * compass(MICROBIT_ID_COMPASS, MAG3110_DEFAULT_ADDR);
  * @endcode
  *
  * Possible Events for the compass are as follows:
  * @code
  * MICROBIT_COMPASS_EVT_CAL_REQUIRED   // triggered when no magnetometer data is available in persistent storage
  * MICROBIT_COMPASS_EVT_CAL_START      // triggered when calibration has begun
  * MICROBIT_COMPASS_EVT_CAL_END        // triggered when calibration has finished.
  * @endcode
  */
MicroBitCompass::MicroBitCompass(uint16_t id, uint16_t address) : average(), sample(), int1(MICROBIT_PIN_COMPASS_DATA_READY)
{
    this->id = id;
    this->address = address;
    
    //we presume it's calibrated until the average values are read.
    this->status = 0x01;
    
    //initialise eventStartTime to 0
    this->eventStartTime = 0;
    
    // Enable automatic reset after each sample;
    writeCommand(MAG_CTRL_REG2, 0xA0);
    
    // Select 10Hz update rate, with oversampling. Also enables the device.
    writeCommand(MAG_CTRL_REG1, 0x61);    
    
    //fetch our previous average values
    average.x = read16(MAG_OFF_X_MSB);
    average.y = read16(MAG_OFF_Y_MSB);
    average.z = read16(MAG_OFF_Z_MSB);
    
    if(average.x == 0 && average.y == 0 && average.z == 0)
        status &= ~MICROBIT_COMPASS_STATUS_CALIBRATED;
    
    // Indicate that we're up and running.
    uBit.flags |= MICROBIT_FLAG_COMPASS_RUNNING;
}

/**
  * Issues a standard, 2 byte I2C command write to the magnetometer.
  * Blocks the calling thread until complete.
  *
  * @param reg The address of the register to write to.
  * @param value The value to write.
  */
void MicroBitCompass::writeCommand(uint8_t reg, uint8_t value)
{
    uint8_t command[2];
    command[0] = reg;
    command[1] = value;
    
    uBit.i2c.write(address, (const char *)command, 2);
}

/**
  * Issues a read command into the specified buffer.
  * Blocks the calling thread until complete.
  *
  * @param reg The address of the register to access.
  * @param buffer Memory area to read the data into.
  * @param length The number of bytes to read.
  */
void MicroBitCompass::readCommand(uint8_t reg, uint8_t* buffer, int length)
{
    uBit.i2c.write(address, (const char *)&reg, 1, true);
    uBit.i2c.read(address, (char *)buffer, length);
}


/**
  * Issues a read of a given address, and returns the value.
  * Blocks the calling thread until complete.
  *
  * @param reg The based address of the 16 bit register to access.
  * @return The register value, interpreted as a 16 but signed value.
  */
int16_t MicroBitCompass::read16(uint8_t reg)
{
    uint8_t cmd[2];

    cmd[0] = reg;
    uBit.i2c.write(address, (const char *)cmd, 1);

    cmd[0] = 0x00;
    cmd[1] = 0x00;
    
    uBit.i2c.read(address, (char *)cmd, 2);
    return (int16_t) ((cmd[1] | (cmd[0] << 8))); //concatenate the MSB and LSB
}

/**
  * Issues a read of a given address, and returns the value.
  * Blocks the calling thread until complete.
  *
  * @param reg The based address of the 16 bit register to access.
  * @return The register value, interpreted as a 8 bi signed value.
  */
int16_t MicroBitCompass::read8(uint8_t reg)
{
    int8_t data;

    data = 0;
    readCommand(reg, (uint8_t*) &data, 1);

    return data;
}


/**
  * Gets the current heading of the device, relative to magnetic north.
  * @return the current heading, in degrees. Or MICROBIT_COMPASS_IS_CALIBRATING if the compass is calibrating. 
  * Or MICROBIT_COMPASS_CALIBRATE_REQUIRED if the compass requires calibration.
  *
  * Example:
  * @code
  * uBit.compass.heading();
  * @endcode
  */
int MicroBitCompass::heading()
{
    if(status & MICROBIT_COMPASS_STATUS_CALIBRATING)
        return MICROBIT_COMPASS_IS_CALIBRATING;    
    else if(!(status & MICROBIT_COMPASS_STATUS_CALIBRATED))
    {
        MicroBitEvent(id, MICROBIT_COMPASS_EVT_CAL_REQUIRED);
        return MICROBIT_COMPASS_CALIBRATE_REQUIRED;
    }
    
    float bearing = (atan2((double)(sample.y - average.y),(double)(sample.x - average.x)))*180/PI;
    if (bearing < 0)
        bearing += 360.0;
        
    return (int) (360.0 - bearing);
}


/**
  * Periodic callback from MicroBit clock.
  * Check if any data is ready for reading by checking the interrupt.
  */
void MicroBitCompass::idleTick()
{
    // Poll interrupt line from accelerometer.
    // Active HI. Interrupt is cleared in data read of MAG_OUT_X_MSB.
    if(int1)
    {
        sample.x = read16(MAG_OUT_X_MSB);
        sample.y = read16(MAG_OUT_Y_MSB);
        sample.z = read16(MAG_OUT_Z_MSB);

        if (status & MICROBIT_COMPASS_STATUS_CALIBRATING)
        {
            minSample.x = min(sample.x, minSample.x);
            minSample.y = min(sample.y, minSample.y);
            minSample.z = min(sample.z, minSample.z);

            maxSample.x = max(sample.x, maxSample.x);
            maxSample.y = max(sample.y, maxSample.y);
            maxSample.z = max(sample.z, maxSample.z);
            
            if(eventStartTime && ticks > eventStartTime + MICROBIT_COMPASS_CALIBRATE_PERIOD)
            {
                eventStartTime = 0;
                calibrateEnd();
            }
        }
    }
}

/**
  * Reads the X axis value of the latest update from the compass.
  * @return The magnetic force measured in the X axis, in no specific units.
  *
  * Example:
  * @code
  * uBit.compass.getX();
  * @endcode
  */
int MicroBitCompass::getX()
{
    return sample.x;
}

/**
  * Reads the Y axis value of the latest update from the compass.
  * @return The magnetic force measured in the Y axis, in no specific units.
  *
  * Example:
  * @code
  * uBit.compass.getY();
  * @endcode
  */     
int MicroBitCompass::getY()
{
    return sample.y;
}

/**
  * Reads the Z axis value of the latest update from the compass.
  * @return The magnetic force measured in the Z axis, in no specific units.
  *
  * Example:
  * @code
  * uBit.compass.getZ();
  * @endcode
  */     
int MicroBitCompass::getZ()
{
    return sample.z;
}

/**
  * Attempts to determine the 8 bit ID from the magnetometer. 
  * @return the id of the compass (magnetometer)
  *
  * Example:
  * @code
  * uBit.compass.whoAmI();
  * @endcode
  */
int MicroBitCompass::whoAmI()
{
    uint8_t data;

    readCommand(MAG_WHOAMI, &data, 1);    
    return (int)data;
}

/**
  * Perform a calibration of the compass.
  * This will fire MICROBIT_COMPASS_EVT_CAL_START.
  * @note THIS MUST BE CALLED TO GAIN RELIABLE VALUES FROM THE COMPASS
  */
void MicroBitCompass::calibrateStart()
{
    if(this->isCalibrating())
        return;

    status |= MICROBIT_COMPASS_STATUS_CALIBRATING;

    // Take a sane snapshot to start with.
    minSample = sample;
    maxSample = sample;

    MicroBitEvent(id, MICROBIT_COMPASS_EVT_CAL_START);
}   

 
/**
  * Perform the asynchronous calibration of the compass.
  * This will fire MICROBIT_COMPASS_EVT_CAL_START and MICROBIT_COMPASS_EVT_CAL_END when finished.
  * @note THIS MUST BE CALLED TO GAIN RELIABLE VALUES FROM THE COMPASS
  */
void MicroBitCompass::calibrateAsync()
{    
    eventStartTime = ticks;
    calibrateStart();
}

/**
  * Complete the calibration of the compass.
  * This will fire MICROBIT_COMPASS_EVT_CAL_END.
  * @note THIS MUST BE CALLED TO GAIN RELIABLE VALUES FROM THE COMPASS
  */     
void MicroBitCompass::calibrateEnd()
{
    average.x = (maxSample.x + minSample.x) / 2;
    average.y = (maxSample.y + minSample.y) / 2;
    average.z = (maxSample.z + minSample.z) / 2;
    
    status &= ~MICROBIT_COMPASS_STATUS_CALIBRATING;
    status |= MICROBIT_COMPASS_STATUS_CALIBRATED;
    
    //Store x, y and z values in persistent storage on the MAG3110
    writeCommand(MAG_OFF_X_LSB, (uint8_t)average.x);
    writeCommand(MAG_OFF_X_MSB, (uint8_t)(average.x >> 8));
    
    writeCommand(MAG_OFF_Y_LSB, (uint8_t)average.y);
    writeCommand(MAG_OFF_Y_MSB, (uint8_t)(average.y >> 8));
    
    writeCommand(MAG_OFF_Z_LSB, (uint8_t)average.z);
    writeCommand(MAG_OFF_Z_MSB, (uint8_t)(average.z >> 8));
    
    MicroBitEvent(id, MICROBIT_COMPASS_EVT_CAL_END);
}    

/**
  * Returns 0 or 1. 1 indicates that the compass is calibrated, zero means the compass requires calibration.
  */
int MicroBitCompass::isCalibrated()
{
    return status & MICROBIT_COMPASS_STATUS_CALIBRATED;   
}

/**
  * Returns 0 or 1. 1 indicates that the compass is calibrating, zero means the compass is not currently calibrating.
  */
int MicroBitCompass::isCalibrating()
{
    return status & MICROBIT_COMPASS_STATUS_CALIBRATING;   
}

/**
  * Clears the calibration held in persistent storage, and sets the calibrated flag to zero.
  */
void MicroBitCompass::clearCalibration()
{
    writeCommand(MAG_OFF_X_LSB, 0);
    writeCommand(MAG_OFF_X_MSB, 0);
    
    writeCommand(MAG_OFF_Y_LSB, 0);
    writeCommand(MAG_OFF_Y_MSB, 0);
    
    writeCommand(MAG_OFF_Z_LSB, 0);
    writeCommand(MAG_OFF_Z_MSB, 0);
    
    status &= ~MICROBIT_COMPASS_STATUS_CALIBRATED;    
}

/**
  * Returns 0 or 1. 1 indicates data is waiting to be read, zero means data is not ready to be read.
  */
int MicroBitCompass::isIdleCallbackNeeded()
{
    //Active HI
    return int1;
}