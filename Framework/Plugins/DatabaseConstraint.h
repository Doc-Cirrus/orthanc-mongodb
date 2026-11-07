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


/**
 * NB: Until 2024-09-09, this file was synchronized with the following
 * folder from the Orthanc main project:
 * https://orthanc.uclouvain.be/hg/orthanc/file/default/OrthancServer/Sources/Search/
 **/


#pragma once

#include "MessagesToolbox.h"

#include <DicomFormat/DicomMap.h>

#include <deque>

namespace OrthancDatabases
{
  class DatabaseConstraint : public boost::noncopyable
  {
  private:
    Orthanc::ResourceType     level_;
    Orthanc::DicomTag         tag_;
    bool                      isIdentifier_;
    ConstraintType            constraintType_;
    std::vector<std::string>  values_;
    bool                      caseSensitive_;
    bool                      mandatory_;

  public:
    DatabaseConstraint(Orthanc::ResourceType level,
                       const Orthanc::DicomTag& tag,
                       bool isIdentifier,
                       ConstraintType type,
                       const std::vector<std::string>& values,
                       bool caseSensitive,
                       bool mandatory);

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    explicit DatabaseConstraint(const OrthancPluginDatabaseConstraint& constraint);
#endif

#if ORTHANC_PLUGINS_HAS_INTEGRATED_FIND == 1
    explicit DatabaseConstraint(const Orthanc::DatabasePluginMessages::DatabaseConstraint& constraint);
#endif

    Orthanc::ResourceType GetLevel() const
    {
      return level_;
    }

    const Orthanc::DicomTag& GetTag() const
    {
      return tag_;
    }

    bool IsIdentifier() const
    {
      return isIdentifier_;
    }

    ConstraintType GetConstraintType() const
    {
      return constraintType_;
    }

    size_t GetValuesCount() const
    {
      return values_.size();
    }

    const std::string& GetValue(size_t index) const;

    const std::string& GetSingleValue() const;

    bool IsCaseSensitive() const
    {
      return caseSensitive_;
    }

    bool IsMandatory() const
    {
      return mandatory_;
    }

    bool IsMatch(const Orthanc::DicomMap& dicom) const;

#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
    void EncodeForPlugins(OrthancPluginDatabaseConstraint& constraint,
                          std::vector<const char*>& tmpValues) const;
#endif
  };


  class DatabaseConstraints : public boost::noncopyable
  {
  private:
    std::deque<DatabaseConstraint*>  constraints_;

  public:
    ~DatabaseConstraints()
    {
      Clear();
    }

    void Clear();

    void AddConstraint(DatabaseConstraint* constraint);  // Takes ownership

    bool IsEmpty() const
    {
      return constraints_.empty();
    }

    size_t GetSize() const
    {
      return constraints_.size();
    }

    const DatabaseConstraint& GetConstraint(size_t index) const;

    std::string Format() const;
  };
}
