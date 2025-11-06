# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
## [0.2.0]

### Added

- 为 `group_permissions` 表的 `group_id` 列添加了索引。
- 为 `group_inheritance` 表的 `group_id` 和 `parent_group_id` 列添加了索引。
- 为 `player_groups` 表的 `expiry_timestamp` 列添加了索引。
- 优化玩家权限组更改缓存失效
- 解决临时组身份过期与缓存一致性问题
- 优化用户组权限获取性能

### Remove
- 去除网页功能