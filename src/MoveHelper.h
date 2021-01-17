#ifndef MOVE_HELPER_H
#define MOVE_HELPER_H

#define STEPS_PER_REV (360.0 / 1.8) ///< [steps / revolution] Steps per Revolution (Motor settings)
#define INCLINATION 2.0             ///< [mm / revolution] Inclination of Spindle
#define STEP_FACTOR (1.0 / 4.0)     ///< step / half-step / quarter-step / 8-step / 16-step

typedef enum
{
    FORWARD = 1,
    BACKWARD = 0
} DIRECTION;

/**
  * @brief Converts Steps to Millimeter
  * @param[in] steps: Steps to convert
  * @retval double Millimeter from conversion
  */
double steps2mm(double steps)
{
    // [mm] =     [steps] *  [mm / rev] / [steps / rev] / Step mode
    return (double)(steps * INCLINATION / STEPS_PER_REV / STEP_FACTOR);
}

/**
  * @brief Converts Millimeter to Steps
  * @param[in] mm: Millimeter to convert
  * @retval double Steps from conversion
  */
double mm2steps(double mm)
{
    // [steps] =    Step mode * [mm] * [steps / rev] / [mm / rev]
    return (double)(STEP_FACTOR * mm * STEPS_PER_REV / INCLINATION);
}

double feedrate2delay(double feedrate)
{
    //                   [steps / s]
    return 1.0 / mm2steps(feedrate / 60.0) / 2.0;
}

#endif /* MOVE_HELPER_H */