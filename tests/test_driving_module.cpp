// O módulo das missões de carro (src/driving/drv.h): a biblioteca nf_drv entra no nf_tests.

#include <cstring>

#include "driving/drv.h"
#include "test_framework.h"

TEST(driving_module_links) { CHECK(std::strcmp(drv::module_name(), "driving") == 0); }
