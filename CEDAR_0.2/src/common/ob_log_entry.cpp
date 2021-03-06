/**
 * Copyright (C) 2013-2016 DaSE .
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * @file ob_log_entry.cpp
 * @brief support multiple clusters for HA by adding or modifying
 *        some functions, member variables;
 *        add set entry for scalable commit
 *
 * @version CEDAR 0.2 
 * @author liubozhong <51141500077@ecnu.cn>
 *         zhouhuan <zhouhuan@stu.ecnu.edu.cn>
 * @date 2015_12_30
 */
/*
 *   (C) 2010-2010 Taobao Inc.
 *
 *   Version: 0.1 $date
 *
 *   Authors:
 *      yanran <yanran.hfs@taobao.com>
 *
 */

#include "ob_log_entry.h"
#include "ob_crc64.h"
#include "utility.h"

using namespace oceanbase::common;

const char* oceanbase::common::DEFAULT_CKPT_EXTENSION = "checkpoint";

int ObLogEntry::fill_header(const char* log_data, const int64_t data_len)
{
  int ret = OB_SUCCESS;

  if ((NULL == log_data && data_len != 0)
      || (NULL != log_data && data_len <= 0))
  {
    ret = OB_INVALID_ARGUMENT;
  }
  else
  {
    header_.set_magic_num(MAGIC_NUMER);
    header_.header_length_ = OB_RECORD_HEADER_LENGTH;
    header_.version_ = LOG_VERSION;
    //add by lbzhong [Max Log Timestamp] 20150824:b
    header_.timestamp_ = tbsys::CTimeUtil::getTime();
    //add:e
    header_.max_cmt_id_ = 0;
    header_.data_length_ = static_cast<int32_t>(sizeof(uint64_t) + sizeof(LogCommand) + data_len);
    header_.data_zlength_ = header_.data_length_;
    if (NULL != log_data)
    {
      header_.data_checksum_ = calc_data_checksum(log_data, data_len);
    }
    else
    {
      header_.data_checksum_ = 0;
    }
    header_.set_header_checksum();
  }

  return ret;
}

//add chujiajia [log synchronization] [multi_cluster] 20160328:b
//fill ups_commit_log header
int ObLogEntry::fill_header(const char* log_data, const int64_t data_len,const int64_t max_cmt_id)
{
  int ret = OB_SUCCESS;

  if ((NULL == log_data && data_len != 0)
      || (NULL != log_data && data_len <= 0))
  {
    ret = OB_INVALID_ARGUMENT;
  }
  else
  {
    header_.set_magic_num(MAGIC_NUMER);
    header_.header_length_ = OB_RECORD_HEADER_LENGTH;
    header_.version_ = LOG_VERSION;
    //add by lbzhong [Max Log Timestamp] 20150824:b
    header_.timestamp_ = tbsys::CTimeUtil::getTime();
    //add:e
    header_.max_cmt_id_ = max_cmt_id;
    header_.data_length_ = static_cast<int32_t>(sizeof(uint64_t) + sizeof(LogCommand) + data_len);
    header_.data_zlength_ = header_.data_length_;
    if (NULL != log_data)
    {
      header_.data_checksum_ = calc_data_checksum(log_data, data_len);
    }
    else
    {
      header_.data_checksum_ = 0;
    }
    header_.set_header_checksum();
  }

  return ret;
}
//add:e
//add by zhouhuan [scalable commit] 20160401:b
int ObLogEntry::set_entry(const char* log_data, const int64_t data_len, const int32_t cmd, const int64_t log_id, const int64_t max_cmt_id)
{
  int err = OB_SUCCESS;
  set_log_seq((uint64_t)log_id);
  set_log_command(cmd);
  err = fill_header(log_data, data_len, max_cmt_id);
  return err;
}
//add e
int64_t ObLogEntry::calc_data_checksum(const char* log_data, const int64_t data_len) const
{
  uint64_t data_checksum = 0;

  if (data_len > 0)
  {
    data_checksum = ob_crc64(reinterpret_cast<const void*>(&seq_), sizeof(seq_));
    data_checksum = ob_crc64(data_checksum, reinterpret_cast<const void*>(&cmd_), sizeof(cmd_));
    data_checksum = ob_crc64(data_checksum, reinterpret_cast<const void*>(log_data), data_len);
  }

  return static_cast<int64_t>(data_checksum);
}

int ObLogEntry::check_header_integrity(bool dump_content) const
{
  int ret = OB_SUCCESS;
  if (OB_SUCCESS != (ret = header_.check_header_checksum())
      || OB_SUCCESS != (ret = header_.check_magic_num(MAGIC_NUMER)))
  {
    TBSYS_LOG(WARN, "check_header_integrity error: ");
    if (dump_content)
    {
      hex_dump(&header_, sizeof(header_), true, TBSYS_LOG_LEVEL_WARN);
    }
  }

  return ret;
}

int ObLogEntry::check_data_integrity(const char* log_data, bool dump_content) const
{
  int ret = OB_SUCCESS;

  if (log_data == NULL)
  {
    ret = OB_INVALID_ARGUMENT;
  }
  else
  {
    int64_t crc_check_sum = calc_data_checksum(log_data, header_.data_length_ - sizeof(uint64_t) - sizeof(LogCommand));
    if ((OB_SUCCESS == header_.check_header_checksum())
          && (OB_SUCCESS == header_.check_magic_num(MAGIC_NUMER))
          && (crc_check_sum == header_.data_checksum_))
    {
      ret = OB_SUCCESS;
    }
    else
    {
      if (dump_content)
      {
        TBSYS_LOG(WARN, "Header: ");
        hex_dump(&header_, sizeof(header_), true, TBSYS_LOG_LEVEL_WARN);
        TBSYS_LOG(WARN, "Body: ");
        hex_dump(log_data - sizeof(uint64_t) - sizeof(LogCommand), header_.data_length_, true, TBSYS_LOG_LEVEL_WARN);
      }
      ret = OB_ERROR;
    }
  }

  return ret;
}

DEFINE_SERIALIZE(ObLogEntry)
{
  int ret = OB_SUCCESS;
  int64_t serialize_size = get_serialize_size();
  if (NULL == buf || (pos + serialize_size) > buf_len)
  {
    ret = OB_ERROR;
  }
  else
  {
    int64_t new_pos = pos;
    if ((OB_SUCCESS == header_.serialize(buf, buf_len, new_pos))
          && (OB_SUCCESS == serialization::encode_i64(buf, buf_len, new_pos, seq_))
          && (OB_SUCCESS == serialization::encode_i32(buf, buf_len, new_pos, cmd_)))
    {
      pos = new_pos;
    }
    else
    {
      ret = OB_ERROR;
    }
  }

  return ret;
}

DEFINE_DESERIALIZE(ObLogEntry)
{
  int ret = OB_SUCCESS;

  int64_t serialize_size = get_serialize_size();
  if (NULL == buf || (serialize_size > data_len - pos))
  {
    ret = OB_ERROR;
  }
  else
  {
    int64_t new_pos = pos;
    if ((OB_SUCCESS == header_.deserialize(buf, data_len, new_pos))
        && (OB_SUCCESS == serialization::decode_i64(buf, data_len, new_pos, (int64_t*)&seq_))
        && (OB_SUCCESS == serialization::decode_i32(buf, data_len, new_pos, (int32_t*)&cmd_)))
    {
      pos = new_pos;
    }
    else
    {
      ret = OB_ERROR;
    }
  }

  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(ObLogEntry)
{
  return header_.get_serialize_size()
          + serialization::encoded_length_i64(seq_)
          + serialization::encoded_length_i32(cmd_);
}

