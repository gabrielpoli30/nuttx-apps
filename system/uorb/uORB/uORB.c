/****************************************************************************
 * apps/system/uorb/uORB/uORB.c
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
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <uORB/uORB.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: orb_open
 *
 * Description:
 *   Open device node as advertiser / subscriber, regist node and save meta
 *   in driver for first user, set buffer number for advertisers.
 *
 * Input Parameters:
 *   meta         The uORB metadata (usually from the ORB_ID() macro)
 *   advertiser   Whether advertiser or subscriber.
 *   instance     Instance number to open.
 *   queue_size   Maximum number of buffered elements.
 *
 * Returned Value:
 *   fd on success, otherwise returns negative value and set errno.
 ****************************************************************************/

static int orb_open(FAR const struct orb_metadata *meta, bool advertiser,
                    int instance, unsigned int queue_size)
{
  char path[ORB_PATH_MAX];
  bool first_open = false;
  int fd;
  int ret;

  snprintf(path, ORB_PATH_MAX, ORB_SENSOR_PATH"%s%d", meta->o_name,
           instance);

  /* Check existance before open */

  ret = access(path, F_OK);
  if (ret < 0)
    {
      struct sensor_reginfo_s reginfo;

      reginfo.path    = path;
      reginfo.esize   = meta->o_size;
      reginfo.nbuffer = queue_size;

      fd = open(ORB_USENSOR_PATH, O_WRONLY);
      if (fd < 0)
        {
          return fd;
        }

      /* Register new device node */

      ret = ioctl(fd, SNIOC_REGISTER, (unsigned long)(uintptr_t)&reginfo);
      close(fd);
      if (ret < 0 && ret != -EEXIST)
        {
          return ret;
        }

      first_open = true;
    }

  fd = open(path, O_CLOEXEC | (advertiser ? O_WRONLY : O_RDONLY));
  if (fd < 0)
    {
      return fd;
    }

  if (first_open)
    {
      ioctl(fd, SNIOC_SET_USERPRIV, (unsigned long)(uintptr_t)meta);
    }

  /* Only first advertiser can successfully set buffer number */

  if (queue_size)
    {
      ioctl(fd, SNIOC_SET_BUFFER_NUMBER, (unsigned long)queue_size);
    }

  return fd;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int orb_advertise_multi_queue(FAR const struct orb_metadata *meta,
                              FAR const void *data, FAR int *instance,
                              unsigned int queue_size)
{
  int inst;
  int fd;

  /* Open the node as an advertiser */

  inst = instance ? *instance : orb_group_count(meta);

  fd = orb_open(meta, true, inst, queue_size);
  if (fd < 0)
    {
      uorberr("%s advertise failed (%i)", meta->o_name, fd);
      return -1;
    }

  /* The advertiser may perform an initial publish to initialise the object */

  if (data != NULL)
    {
      int ret;

      ret = orb_publish_multi(fd, data, meta->o_size);
      if (ret != meta->o_size)
        {
          uorberr("%s publish %d, expect %d",
                  meta->o_name, ret, meta->o_size);
          close(fd);
          return -1;
        }
    }

  return fd;
}

int orb_unadvertise(int fd)
{
  return close(fd);
}

ssize_t orb_publish_multi(int fd, const void *data, size_t len)
{
  return write(fd, data, len);
}

int orb_subscribe_multi(FAR const struct orb_metadata *meta,
                        unsigned instance)
{
  return orb_open(meta, false, instance, 0);
}

int orb_unsubscribe(int fd)
{
  return close(fd);
}

ssize_t orb_copy_multi(int fd, FAR void *buffer, size_t len)
{
  return read(fd, buffer, len);
}

int orb_get_state(int fd, FAR struct orb_state *state)
{
  struct sensor_state_s tmp;
  int ret;

  if (!state)
    {
      return -EINVAL;
    }

  ret = ioctl(fd, SNIOC_GET_STATE, (unsigned long)(uintptr_t)&tmp);
  if (ret < 0)
    {
      return ret;
    }

  state->max_frequency      = tmp.min_interval ?
                              1000000 / tmp.min_interval : 0;
  state->min_batch_interval = tmp.min_latency;
  state->queue_size         = tmp.nbuffer;
  state->nsubscribers       = tmp.nsubscribers;
  state->generation         = tmp.generation;

  return ret;
}

int orb_check(int fd, FAR bool *updated)
{
  struct pollfd fds[1];
  int ret;

  fds[0].fd     = fd;
  fds[0].events = POLLIN;

  ret = poll(fds, 1, 0);
  if (ret < 0)
    {
      return -1;
    }

  *updated = (fds[0].revents & POLLIN) > 0;
  return 0;
}

int orb_ioctl(int handle, int cmd, unsigned long arg)
{
  return ioctl(handle, cmd, arg);
}

int orb_set_interval(int fd, unsigned interval)
{
  return ioctl(fd, SNIOC_SET_INTERVAL, (unsigned long)interval);
}

int orb_get_interval(int fd, FAR unsigned *interval)
{
  struct sensor_state_s tmp;
  int ret;

  ret = ioctl(fd, SNIOC_GET_STATE, (unsigned long)(uintptr_t)&tmp);
  if (ret < 0)
    {
      return ret;
    }

  *interval = tmp.min_interval;
  return ret;
}

int orb_set_batch_interval(int fd, unsigned batch_interval)
{
  return ioctl(fd, SNIOC_BATCH, (unsigned long)batch_interval);
}

int orb_get_batch_interval(int fd, FAR unsigned *batch_interval)
{
  struct sensor_state_s tmp;
  int ret;

  ret = ioctl(fd, SNIOC_GET_STATE, (unsigned long)(uintptr_t)&tmp);
  if (ret < 0)
    {
      return ret;
    }

  *batch_interval = tmp.min_latency;
  return ret;
}

orb_abstime orb_absolute_time(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  return 1000000ull * ts.tv_sec + ts.tv_nsec / 1000;
}

int orb_exists(FAR const struct orb_metadata *meta, int instance)
{
  struct sensor_state_s state;
  char path[ORB_PATH_MAX];
  int ret;
  int fd;

  snprintf(path, ORB_PATH_MAX, ORB_SENSOR_PATH"%s%d", meta->o_name,
           instance);
  fd = open(path, 0);
  if (fd < 0)
    {
      return -1;
    }

  ret = ioctl(fd, SNIOC_GET_STATE, (unsigned long)(uintptr_t)&state);
  close(fd);
  if (ret < 0)
    {
      return -1;
    }

  return state.nadvertisers > 0 ? 0 : -1;
}

int orb_group_count(FAR const struct orb_metadata *meta)
{
  unsigned instance = 0;

  while (orb_exists(meta, instance) == 0)
    {
      ++instance;
    }

  return instance;
}