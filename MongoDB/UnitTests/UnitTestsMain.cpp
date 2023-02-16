/**
 * MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 - 2023  (Doc Cirrus GmbH)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include "../../Framework/MongoDB/MongoDatabase.h"
#include "../Plugins/MongoDBIndex.h"

#include <Compatibility.h>  // For std::unique_ptr<>
#include <Logging.h>
#include <SystemToolbox.h>

#include <gtest/gtest.h>


// #include "../../Framework/Plugins/IndexUnitTests.h"


TEST(MongoDBIndex, Lock)
{

}


TEST(MongoDB, ImplicitTransaction)
{

}


int main(int argc, char **argv)
{
  return 0;
}
