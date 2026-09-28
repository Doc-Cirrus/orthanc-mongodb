/**
 * MongoDB Plugin - A plugin for Orthanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 - 2026  (Doc Cirrus GmbH)
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

#include "../Plugins/MongoDBIndex.h"
#include "MongoDBTestsToolbox.h"

#include <Compatibility.h>  // For std::unique_ptr<>
#include <Logging.h>
#include <SystemToolbox.h>

#include <gtest/gtest.h>

#include "../../Framework/Plugins/IndexUnitTests.h"


int main(int argc, char **argv)
{
  // Removes the "--gtest_*" arguments
  ::testing::InitGoogleTest(&argc, argv);

  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " <connection-uri> [gtest options]"
              << std::endl << std::endl
              << "Each test uses its own \"test_<uuid>\" database on this server, and drops it afterwards."
              << std::endl << std::endl
              << "Example: " << argv[0] << " mongodb://localhost:27017/" << std::endl
              << "Example: " << argv[0] << " \"mongodb://localhost:27018/?replicaSet=rs0\"" << std::endl
              << std::endl;
    return -1;
  }

  mongocxx::instance instance;

  OrthancDatabases::SetTestConnectionUri(argv[1]);

  Orthanc::Logging::Initialize();
  Orthanc::Logging::EnableInfoLevel(true);
  // Orthanc::Logging::EnableTraceLevel(true);

  std::cout << "MongoDB server: " << argv[1]
            << (OrthancDatabases::IsTestServerReplicaSet() ? " (replica set)" : " (standalone)") << std::endl;

  int result = RUN_ALL_TESTS();

  Orthanc::Logging::Finalize();

  return result;
}
