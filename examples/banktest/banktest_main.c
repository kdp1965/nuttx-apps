/****************************************************************************
 * apps/examples/banktest/banktest_main.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/* banktest [seconds] [bytes]: a task that lives a while with some heap of
 * its own - several in the background spread over the LISA chip's memory
 * banks (CONFIG_LISA_TASK_BANKS); it checks its allocation survived the
 * sleep and says where its stack and heap are.
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, FAR char *argv[])
{
  int seconds = argc > 1 ? atoi(argv[1]) : 5;
  int bytes   = argc > 2 ? atoi(argv[2]) : 1024;
  FAR unsigned char *p;
  int local;
  int i;
  int bad = 0;

  p = malloc(bytes);
  if (p == NULL)
    {
      printf("banktest %d: malloc(%d) failed\n", getpid(), bytes);
      return 1;
    }

  /* A pattern that differs between bytes 0x2000 apart: an alias of the
   * data window and the stack window would pass a plain i + pid
   */

  for (i = 0; i < bytes; i++)
    {
      p[i] = (unsigned char)(i + (i >> 8) * 3 + getpid());
    }

  printf("banktest %d: stack %p heap %p, %d bytes, sleeping %d s\n",
         getpid(), &local, p, bytes, seconds);
  sleep(seconds);

  for (i = 0; i < bytes; i++)
    {
      if (p[i] != (unsigned char)(i + (i >> 8) * 3 + getpid()))
        {
          bad++;
        }
    }

  printf("banktest %d: done, %d bad bytes\n", getpid(), bad);
  free(p);
  return bad ? 1 : 0;
}
