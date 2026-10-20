#pragma once
#include "magpie_model.h"
void magpie_ui_create(void);
void magpie_ui_update(const magpie_model_t *model, uint64_t now, int battery, uint32_t passkey, const char *toast);
