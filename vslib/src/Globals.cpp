#include "Globals.h"

std::recursive_mutex saivs::Globals::apimutex;

bool saivs::Globals::apiInitialized = false;
