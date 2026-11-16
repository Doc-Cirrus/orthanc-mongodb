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


#include "MessagesToolbox.h"


namespace OrthancDatabases
{
  namespace MessagesToolbox
  {
    Orthanc::ResourceType Convert(Orthanc::DatabasePluginMessages::ResourceType resourceType)
    {
      switch (resourceType)
      {
        case Orthanc::DatabasePluginMessages::RESOURCE_PATIENT:
          return Orthanc::ResourceType_Patient;

        case Orthanc::DatabasePluginMessages::RESOURCE_STUDY:
          return Orthanc::ResourceType_Study;

        case Orthanc::DatabasePluginMessages::RESOURCE_SERIES:
          return Orthanc::ResourceType_Series;

        case Orthanc::DatabasePluginMessages::RESOURCE_INSTANCE:
          return Orthanc::ResourceType_Instance;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }
    }


    OrthancPluginResourceType ConvertToPlainC(Orthanc::ResourceType type)
    {
      switch (type)
      {
        case Orthanc::ResourceType_Patient:
          return OrthancPluginResourceType_Patient;

        case Orthanc::ResourceType_Study:
          return OrthancPluginResourceType_Study;

        case Orthanc::ResourceType_Series:
          return OrthancPluginResourceType_Series;

        case Orthanc::ResourceType_Instance:
          return OrthancPluginResourceType_Instance;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }
    }


    Orthanc::ResourceType Convert(OrthancPluginResourceType type)
    {
      switch (type)
      {
        case OrthancPluginResourceType_Patient:
          return Orthanc::ResourceType_Patient;

        case OrthancPluginResourceType_Study:
          return Orthanc::ResourceType_Study;

        case OrthancPluginResourceType_Series:
          return Orthanc::ResourceType_Series;

        case OrthancPluginResourceType_Instance:
          return Orthanc::ResourceType_Instance;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }
    }


#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    OrthancPluginConstraintType ConvertToPlainC(ConstraintType constraint)
    {
      switch (constraint)
      {
        case ConstraintType_Equal:
          return OrthancPluginConstraintType_Equal;

        case ConstraintType_GreaterOrEqual:
          return OrthancPluginConstraintType_GreaterOrEqual;

        case ConstraintType_SmallerOrEqual:
          return OrthancPluginConstraintType_SmallerOrEqual;

        case ConstraintType_Wildcard:
          return OrthancPluginConstraintType_Wildcard;

        case ConstraintType_List:
          return OrthancPluginConstraintType_List;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }
    }
#endif


#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    ConstraintType Convert(OrthancPluginConstraintType constraint)
    {
      switch (constraint)
      {
        case OrthancPluginConstraintType_Equal:
          return ConstraintType_Equal;

        case OrthancPluginConstraintType_GreaterOrEqual:
          return ConstraintType_GreaterOrEqual;

        case OrthancPluginConstraintType_SmallerOrEqual:
          return ConstraintType_SmallerOrEqual;

        case OrthancPluginConstraintType_Wildcard:
          return ConstraintType_Wildcard;

        case OrthancPluginConstraintType_List:
          return ConstraintType_List;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }
    }
#endif


    Orthanc::DatabasePluginMessages::ResourceType ConvertToProtobuf(OrthancPluginResourceType resourceType)
    {
      switch (resourceType)
      {
        case OrthancPluginResourceType_Patient:
          return Orthanc::DatabasePluginMessages::RESOURCE_PATIENT;

        case OrthancPluginResourceType_Study:
          return Orthanc::DatabasePluginMessages::RESOURCE_STUDY;

        case OrthancPluginResourceType_Series:
          return Orthanc::DatabasePluginMessages::RESOURCE_SERIES;

        case OrthancPluginResourceType_Instance:
          return Orthanc::DatabasePluginMessages::RESOURCE_INSTANCE;

        default:
          throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
      }
    }
  }
}
