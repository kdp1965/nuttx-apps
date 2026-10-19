############################################################################
# apps/examples/banktest/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
############################################################################

include $(APPDIR)/Make.defs

# banktest: a task with the default stack (a shared stack bank, its heap
# the 8 KB granule); bigtest: the same program with a 4 KB stack, which
# gets a bank of its own and a heap of 24 KB less the stack (arch/lisa,
# CONFIG_LISA_TASK_OWN_BANK_STACK)
PROGNAME  = banktest bigtest
PRIORITY  = 100 100
STACKSIZE = $(CONFIG_DEFAULT_TASK_STACKSIZE) 4096
MODULE    = $(CONFIG_EXAMPLES_BANKTEST)

MAINSRC = banktest_main.c bigtest_main.c

include $(APPDIR)/Application.mk
