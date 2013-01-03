/*
 * FileMonitorImpl.hpp
 *
 * Copyright (C) 2009-12 by RStudio, Inc.
 *
 * This program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

#ifndef CORE_SYSTEM_FILE_MONITOR_IMPL_HPP
#define CORE_SYSTEM_FILE_MONITOR_IMPL_HPP

#include <string>
#include <algorithm>
#include <list>

#include <boost/bind.hpp>

#include <core/FilePath.hpp>
#include <core/collection/Tree.hpp>

#include <core/system/FileChangeEvent.hpp>

#include <core/system/FileMonitor.hpp>

namespace core {   
namespace system {
namespace file_monitor {
namespace impl {

Error processFileAdded(
               tcl::unique_tree<FileInfo>::iterator parentIt,
               const FileChangeEvent& fileChange,
               bool recursive,
               const boost::function<bool(const FileInfo&)>& filter,
               const boost::function<Error(const FileInfo&)>& onBeforeScanDir,
               tcl::unique_tree<FileInfo>* pTree,
               std::vector<FileChangeEvent>* pFileChanges);

inline Error processFileAdded(
               tcl::unique_tree<FileInfo>::iterator parentIt,
               const FileChangeEvent& fileChange,
               bool recursive,
               const boost::function<bool(const FileInfo&)>& filter,
               tcl::unique_tree<FileInfo>* pTree,
               std::vector<FileChangeEvent>* pFileChanges)
{
   return processFileAdded(parentIt,
                           fileChange,
                           recursive,
                           filter,
                           boost::function<Error(const FileInfo&)>(),
                           pTree,
                           pFileChanges);
}

void processFileModified(tcl::unique_tree<FileInfo>::iterator parentIt,
                         const FileChangeEvent& fileChange,
                         tcl::unique_tree<FileInfo>* pTree,
                         std::vector<FileChangeEvent>* pFileChanges);

void processFileRemoved(tcl::unique_tree<FileInfo>::iterator parentIt,
                        const FileChangeEvent& fileChange,
                        bool recursive,
                        tcl::unique_tree<FileInfo>* pTree,
                        std::vector<FileChangeEvent>* pFileChanges);

Error discoverAndProcessFileChanges(
   const FileInfo& fileInfo,
   bool recursive,
   const boost::function<bool(const FileInfo&)>& filter,
   const boost::function<Error(const FileInfo&)>& onBeforeScanDir,
   tcl::unique_tree<FileInfo>* pTree,
   const boost::function<void(const std::vector<FileChangeEvent>&)>&
                                                            onFilesChanged);

inline Error discoverAndProcessFileChanges(
   const FileInfo& fileInfo,
   bool recursive,
   const boost::function<bool(const FileInfo&)>& filter,
   tcl::unique_tree<FileInfo>* pTree,
   const boost::function<void(const std::vector<FileChangeEvent>&)>&
                                                            onFilesChanged)
{
   return discoverAndProcessFileChanges(
                                 fileInfo,
                                 recursive,
                                 filter,
                                 boost::function<Error(const FileInfo&)>(),
                                 pTree,
                                 onFilesChanged);
}

std::list<void*> activeEventContexts();


} // namespace impl
} // namespace file_monitor
} // namespace system
} // namespace core 

#endif // CORE_SYSTEM_FILE_MONITOR_IMPL_HPP


