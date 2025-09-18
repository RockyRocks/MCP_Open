#pragma once
#include "ICommandStrategy.h"
#include <memory>

std::shared_ptr<ICommandStrategy> createEchoCommand();
