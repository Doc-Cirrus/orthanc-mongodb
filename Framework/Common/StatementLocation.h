// Taken from https://hg.orthanc-server.com/orthanc-databases/

#pragma once

#define STATEMENT_FROM_HERE  ::OrthancDatabases::StatementLocation(__FILE__, __LINE__)


namespace OrthancDatabases
{
  class StatementLocation
  {
  private:
    const char* file_;
    int line_;

    StatementLocation(); // Forbidden

  public:
    StatementLocation(const char* file,
                      int line) :
      file_(file),
      line_(line)
    {
    }

    const char* GetFile() const
    {
      return file_;
    }

    int GetLine() const
    {
      return line_;
    }

    bool operator< (const StatementLocation& other) const;
  };
}
