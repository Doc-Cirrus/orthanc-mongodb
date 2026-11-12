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


#include "DatabaseConstraint.h"

#include "../../Resources/Orthanc/Plugins/OrthancPluginException.h"

#include <boost/lexical_cast.hpp>
#include <cassert>


namespace OrthancDatabases
{
  DatabaseConstraint::DatabaseConstraint(Orthanc::ResourceType level,
                                         const Orthanc::DicomTag& tag,
                                         bool isIdentifier,
                                         ConstraintType type,
                                         const std::vector<std::string>& values,
                                         bool caseSensitive,
                                         bool mandatory) :
    level_(level),
    tag_(tag),
    isIdentifier_(isIdentifier),
    constraintType_(type),
    values_(values),
    caseSensitive_(caseSensitive),
    mandatory_(mandatory)
  {
    if (type != ConstraintType_List &&
        values_.size() != 1)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
  }


#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
  DatabaseConstraint::DatabaseConstraint(const OrthancPluginDatabaseConstraint& constraint) :
    level_(MessagesToolbox::Convert(constraint.level)),
    tag_(constraint.tagGroup, constraint.tagElement),
    isIdentifier_(constraint.isIdentifierTag),
    constraintType_(MessagesToolbox::Convert(constraint.type)),
    caseSensitive_(constraint.isCaseSensitive),
    mandatory_(constraint.isMandatory)
  {
    if (constraintType_ != ConstraintType_List &&
        constraint.valuesCount != 1)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    values_.resize(constraint.valuesCount);

    for (uint32_t i = 0; i < constraint.valuesCount; i++)
    {
      assert(constraint.values[i] != NULL);
      values_[i].assign(constraint.values[i]);
    }
  }
#endif


#if ORTHANC_PLUGINS_HAS_INTEGRATED_FIND == 1
  DatabaseConstraint::DatabaseConstraint(const Orthanc::DatabasePluginMessages::DatabaseConstraint& constraint) :
    level_(MessagesToolbox::Convert(constraint.level())),
    tag_(constraint.tag_group(), constraint.tag_element()),
    isIdentifier_(constraint.is_identifier_tag()),
    caseSensitive_(constraint.is_case_sensitive()),
    mandatory_(constraint.is_mandatory())
  {
    switch (constraint.type())
    {
      case Orthanc::DatabasePluginMessages::CONSTRAINT_EQUAL:
        constraintType_ = ConstraintType_Equal;
        break;

      case Orthanc::DatabasePluginMessages::CONSTRAINT_SMALLER_OR_EQUAL:
        constraintType_ = ConstraintType_SmallerOrEqual;
        break;

      case Orthanc::DatabasePluginMessages::CONSTRAINT_GREATER_OR_EQUAL:
        constraintType_ = ConstraintType_GreaterOrEqual;
        break;

      case Orthanc::DatabasePluginMessages::CONSTRAINT_WILDCARD:
        constraintType_ = ConstraintType_Wildcard;
        break;

      case Orthanc::DatabasePluginMessages::CONSTRAINT_LIST:
        constraintType_ = ConstraintType_List;
        break;

      default:
        throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    if (constraintType_ != ConstraintType_List &&
        constraint.values().size() != 1)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }

    values_.resize(constraint.values().size());

    for (int i = 0; i < constraint.values().size(); i++)
    {
      values_[i] = constraint.values(i);
    }
  }
#endif


  const std::string& DatabaseConstraint::GetValue(size_t index) const
  {
    if (index >= values_.size())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
    else
    {
      return values_[index];
    }
  }


  const std::string& DatabaseConstraint::GetSingleValue() const
  {
    if (values_.size() != 1)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_BadSequenceOfCalls);
    }
    else
    {
      return values_[0];
    }
  }


#if ORTHANC_PLUGINS_HAS_DATABASE_CONSTRAINT == 1
  void DatabaseConstraint::EncodeForPlugins(OrthancPluginDatabaseConstraint& constraint,
                                            std::vector<const char*>& tmpValues) const
  {
    memset(&constraint, 0, sizeof(constraint));

    tmpValues.resize(values_.size());

    for (size_t i = 0; i < values_.size(); i++)
    {
      tmpValues[i] = values_[i].c_str();
    }

    constraint.level = MessagesToolbox::ConvertToPlainC(level_);
    constraint.tagGroup = tag_.GetGroup();
    constraint.tagElement = tag_.GetElement();
    constraint.isIdentifierTag = isIdentifier_;
    constraint.isCaseSensitive = caseSensitive_;
    constraint.isMandatory = mandatory_;
    constraint.type = MessagesToolbox::ConvertToPlainC(constraintType_);
    constraint.valuesCount = values_.size();
    constraint.values = (tmpValues.empty() ? NULL : &tmpValues[0]);
  }
#endif


  void DatabaseConstraints::Clear()
  {
    for (size_t i = 0; i < constraints_.size(); i++)
    {
      assert(constraints_[i] != NULL);
      delete constraints_[i];
    }

    constraints_.clear();
  }


  void DatabaseConstraints::AddConstraint(DatabaseConstraint* constraint)
  {
    if (constraint == NULL)
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_NullPointer);
    }
    else
    {
      constraints_.push_back(constraint);
    }
  }


  const DatabaseConstraint& DatabaseConstraints::GetConstraint(size_t index) const
  {
    if (index >= constraints_.size())
    {
      throw Orthanc::OrthancException(Orthanc::ErrorCode_ParameterOutOfRange);
    }
    else
    {
      assert(constraints_[index] != NULL);
      return *constraints_[index];
    }
  }


  std::string DatabaseConstraints::Format() const
  {
    std::string s;

    for (size_t i = 0; i < constraints_.size(); i++)
    {
      assert(constraints_[i] != NULL);
      const DatabaseConstraint& constraint = *constraints_[i];
      s += "Constraint " + boost::lexical_cast<std::string>(i) + " at " + EnumerationToString(constraint.GetLevel()) +
        ": " + constraint.GetTag().Format();

      switch (constraint.GetConstraintType())
      {
        case ConstraintType_Equal:
          s += " == " + constraint.GetSingleValue();
          break;

        case ConstraintType_SmallerOrEqual:
          s += " <= " + constraint.GetSingleValue();
          break;

        case ConstraintType_GreaterOrEqual:
          s += " >= " + constraint.GetSingleValue();
          break;

        case ConstraintType_Wildcard:
          s += " ~~ " + constraint.GetSingleValue();
          break;

        case ConstraintType_List:
        {
          s += " in [ ";
          bool first = true;
          for (size_t j = 0; j < constraint.GetValuesCount(); j++)
          {
            if (first)
            {
              first = false;
            }
            else
            {
              s += ", ";
            }
            s += constraint.GetValue(j);
          }
          s += "]";
          break;
        }

        default:
          PLUGIN_THROW_WITH_FILE_AND_LINE_INFO(Orthanc::ErrorCode_NotImplemented);
      }

      s += "\n";
    }

    return s;
  }
}
