/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2026 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2026 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/


#pragma once

#include <orthanc/OrthancCDatabasePlugin.h>
#include <OrthancDatabasePlugin.pb.h>

// Ensure that "ORTHANC_PLUGINS_VERSION_IS_ABOVE" is defined
#include "../../Resources/Orthanc/Plugins/OrthancPluginCppWrapper.h"

#define ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT 0

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 5, 2)
#    undef  ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT
#    define ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT 1
#  endif
#endif


#define ORTHANC_PLUGINS_HAS_INTEGRATED_FIND 0
#define ORTHANC_PLUGINS_HAS_CHANGES_EXTENDED 0

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 5)
#    undef  ORTHANC_PLUGINS_HAS_INTEGRATED_FIND
#    define ORTHANC_PLUGINS_HAS_INTEGRATED_FIND 1
#    undef  ORTHANC_PLUGINS_HAS_CHANGES_EXTENDED
#    define ORTHANC_PLUGINS_HAS_CHANGES_EXTENDED 1
#  endif
#endif

#define ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA 0
#define ORTHANC_PLUGINS_HAS_KEY_VALUE_STORES 0
#define ORTHANC_PLUGINS_HAS_QUEUES 0
#define ORTHANC_PLUGINS_HAS_RESERVE_QUEUE_VALUE 0
#define ORTHANC_PLUGINS_HAS_AUDIT_LOGS 0

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 8)
#    undef  ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA
#    define ORTHANC_PLUGINS_HAS_ATTACHMENTS_CUSTOM_DATA 1
#    undef  ORTHANC_PLUGINS_HAS_KEY_VALUE_STORES
#    define ORTHANC_PLUGINS_HAS_KEY_VALUE_STORES 1
#    undef  ORTHANC_PLUGINS_HAS_QUEUES
#    define ORTHANC_PLUGINS_HAS_QUEUES 1
#  endif
#endif


#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 9)
#    undef  ORTHANC_PLUGINS_HAS_AUDIT_LOGS
#    define ORTHANC_PLUGINS_HAS_AUDIT_LOGS 1
#  endif
#endif

#if defined(ORTHANC_PLUGINS_VERSION_IS_ABOVE)
#  if ORTHANC_PLUGINS_VERSION_IS_ABOVE(1, 12, 10)
#    undef  ORTHANC_PLUGINS_HAS_RESERVE_QUEUE_VALUE
#    define ORTHANC_PLUGINS_HAS_RESERVE_QUEUE_VALUE 1
#  endif
#endif


#include <Enumerations.h>


namespace OrthancDatabases
{
  enum ConstraintType
  {
    ConstraintType_Equal,
    ConstraintType_SmallerOrEqual,
    ConstraintType_GreaterOrEqual,
    ConstraintType_Wildcard,
    ConstraintType_List
  };

  namespace MessagesToolbox
  {
    Orthanc::ResourceType Convert(Orthanc::DatabasePluginMessages::ResourceType resourceType);

    OrthancPluginResourceType ConvertToPlainC(Orthanc::ResourceType type);

    Orthanc::ResourceType Convert(OrthancPluginResourceType type);

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    OrthancPluginConstraintType ConvertToPlainC(ConstraintType constraint);
#endif

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    ConstraintType Convert(OrthancPluginConstraintType constraint);
#endif
  }
}
