add_subdirectory(ccp)
add_subdirectory(tdma)
add_subdirectory(cir)
#add_subdirectory(pan)
add_subdirectory(survey)
add_subdirectory(dsp)
add_subdirectory(euclid)
add_subdirectory(rng)
add_subdirectory(twr_ss)
add_subdirectory(twr_ss_ext)
add_subdirectory(twr_ds)
add_subdirectory(twr_ds_ext)
add_subdirectory(nrng)
add_subdirectory(twr_ss_nrng)

# Install syscfg

   

install(DIRECTORY "${PROJECT_BINARY_DIR}/../bin/targets/syscfg/generated/include" DESTINATION include/

        FILES_MATCHING PATTERN "*.h"
)
