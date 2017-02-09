/**
 * MongoDB Plugin - A plugin for Otrhanc DICOM Server for storing DICOM data in MongoDB Database
 * Copyright (C) 2017 DocCirrus, Germany
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General 
 * Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/

#include "gtest/gtest.h"

#include "Configuration.h"
#include "MongoDBConnection.h"
#include "MongoDBBackend.h"

static OrthancPluginContext static_context;

OrthancPluginErrorCode PluginServiceMock(struct _OrthancPluginContext_t* context,
                                              _OrthancPluginService service,
                                              const void* params)
{
    if (service == _OrthancPluginService_GetExpectedDatabaseVersion) {
        _OrthancPluginReturnSingleValue *p = static_cast<_OrthancPluginReturnSingleValue *>(
                                                    const_cast<void *>(params));
        (*p->resultUint32) = 6;
    }
    return OrthancPluginErrorCode_Success;
}

TEST (Indexplugin, Constructor) {
    static_context.InvokeService = &PluginServiceMock;

    OrthancPlugins::MongoDBConnection connection;
    connection.SetConnectionUri("mongodb://localhost:27011/pliugin_unit_tests");

    OrthancPlugins::MongoDBBackend bakend(&static_context, &connection);

    ASSERT_EQ (-1, -1);
}
 
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}