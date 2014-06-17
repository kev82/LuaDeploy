#!/bin/bash

echo "#!/usr/bin/env lua"
echo "local launcher_template_file = [========["
cat launcher.lt
echo "]========]"
echo "local app = {}"
cat launchtemplate.lua
cat installapp.lua
cat exportsql.lua
cat app.lua
