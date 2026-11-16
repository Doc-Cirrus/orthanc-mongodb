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

#include <DicomFormat/DicomTag.h>

namespace OrthancDatabases
{
  class IdentifierTag
  {
  private:
    Orthanc::ResourceType  level_;
    Orthanc::DicomTag      tag_;
    std::string            name_;

  public:
    IdentifierTag(Orthanc::ResourceType level,
                  const Orthanc::DicomTag& tag,
                  const std::string& name) :
      level_(level),
      tag_(tag),
      name_(name)
    {
    }

    Orthanc::ResourceType GetLevel() const
    {
      return level_;
    }

    const Orthanc::DicomTag& GetTag() const
    {
      return tag_;
    }

    const std::string& GetName() const
    {
      return name_;
    }
  };
}
