#ifndef SO_PROJEKT_DYREKTOR_H
#define SO_PROJEKT_DYREKTOR_H

#include "../common.h"

int dyrektor_main(HoursOpen hours_open, const std::array<uint32_t, 5>& department_limits, int time_mul,
				  int gen_min_delay_sec, int gen_max_delay_sec, int gen_max_count, bool spawn_generator, bool one_day);

#endif //SO_PROJEKT_DYREKTOR_H
