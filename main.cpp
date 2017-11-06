#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdio.h>
#include <string>

#include <chrono>
#include <thread> // needed for sleep_for

#include "registry/ApiRegistry.hpp"

// ----------------------------------------------------------------------

#include "traffic_light/traffic_light.h"
#include "logger/logger.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

#ifdef PLUGIN_TRAFFIC_LIGHT_STATIC
	Registry::addApiStatic<pal_traffic_light_i>();
#else
	Registry::addApiDynamic<pal_traffic_light_api>(true);
#endif

#ifdef PLUGIN_LOGGER_STATIC
	Registry::addApiStatic<pal_logger_i>();
#else
	Registry::addApiDynamic<pal_logger_api>(true);
#endif

	pal::TrafficLight trafficLight{};

	for ( ;; ) {

		Registry::pollForDynamicReload();

		trafficLight.step();

		pal::Logger() << trafficLight.getStateAsString();

		std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
	};
}
