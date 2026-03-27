#pragma once
#include <commands/ICommandStrategy.h>
#include <memory>

std::shared_ptr<ICommandStrategy> CreateEchoCommand();
