/**
 * Resource Management with unique_ptr and Custom Deleters
 *
 * Demonstrates RAII principle by wrapping C-style resources (FILE*, sqlite3*)
 * in unique_ptr with custom deleters to ensure automatic cleanup.
 *
 * Key Concepts:
 * - Custom deleters as function objects or lambdas
 * - Type-erased deleters in unique_ptr
 * - Cleanup verification on all exit paths
 * - Exception safety guarantees
 */

#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <sqlite3.h>
#include <stdexcept>
#include <string>

// ============================================================================
// SECTION 1: Custom Deleters
// ============================================================================

// --- FILE* Deleter ---
struct FileDeleter
{
   void operator()(FILE *file) const noexcept
   {
      if (file != nullptr)
      {
         std::fclose(file);
         std::cout << "[FileDeleter] File closed successfully\n";
      }
   }
};

// Alternative: Lambda-based deleter (more flexible for inline use)
using UniqueFilePtr = std::unique_ptr<FILE, std::function<void(FILE *)>>;

UniqueFilePtr make_unique_file(const char *filename, const char *mode)
{
   FILE *file = std::fopen(filename, mode);
   if (!file)
   {
      throw std::runtime_error(std::string("Failed to open file: ") + filename);
   }
   return UniqueFilePtr(file,
                        [](FILE *f)
                        {
                           if (f)
                           {
                              std::fclose(f);
                              std::cout << "[LambdaDeleter] File closed\n";
                           }
                        });
}

// ============================================================================
// SECTION 2: SQLite Deleter
// ============================================================================

// Forward declaration for SQLite
// struct sqlite3;
// int sqlite3_open(const char *, sqlite3 **);
// int sqlite3_close(sqlite3 *);
// int sqlite3_exec(sqlite3 *, const char *, int (*callback)(void *, int, char **, char **), void *,
//                 char **);
// const char *sqlite3_errmsg(sqlite3 *);
// int sqlite3_finalize(sqlite3_stmt *);
// struct sqlite3_stmt;
// int sqlite3_prepare_v2(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
// int sqlite3_step(sqlite3_stmt *);
// int sqlite3_column_int(sqlite3_stmt *, int);
// const char *sqlite3_column_text(sqlite3_stmt *, int);

struct SqliteDeleter
{
   void operator()(sqlite3 *db) const noexcept
   {
      if (db != nullptr)
      {
         // sqlite3_close returns 0 on success, but we log anyway
         int result = sqlite3_close(db);
         if (result == SQLITE_OK)
         {
            std::cout << "[SqliteDeleter] Database closed successfully\n";
         }
         else
         {
            std::cout << "[SqliteDeleter] Database close returned: " << result << "\n";
         }
      }
   }
};

// ============================================================================
// SECTION 3: RAII Resource Wrapper Classes
// ============================================================================

/**
 * SafeFileWrapper - RAII wrapper for FILE*
 *
 * Guarantees:
 * - File is closed when object is destroyed
 * - Works with unique_ptr automatically
 * - Exception-safe cleanup
 */
class SafeFileWrapper
{
 public:
   SafeFileWrapper(const std::string &filename, const char *mode)
   {
      file_.reset(std::fopen(filename.c_str(), mode));
      if (!file_)
      {
         throw std::runtime_error("Failed to open: " + filename);
      }
      std::cout << "[SafeFileWrapper] Opened: " << filename << "\n";
   }

   // Disable copy (unique ownership)
   SafeFileWrapper(const SafeFileWrapper &) = delete;
   SafeFileWrapper &operator=(const SafeFileWrapper &) = delete;

   // Allow move
   SafeFileWrapper(SafeFileWrapper &&) = default;
   SafeFileWrapper &operator=(SafeFileWrapper &&) = default;

   FILE *get() const
   {
      return file_.get();
   }
   FILE *release()
   {
      return file_.release();
   }

   void write(const std::string &data)
   {
      if (!file_)
         throw std::runtime_error("File not open");
      std::fputs(data.c_str(), file_.get());
      std::fflush(file_.get());
   }

   std::string read()
   {
      if (!file_)
         throw std::runtime_error("File not open");
      std::string result;
      char buffer[256];
      while (std::fgets(buffer, sizeof(buffer), file_.get()) != nullptr)
      {
         result += buffer;
      }
      return result;
   }

   bool isOpen() const
   {
      return file_ != nullptr;
   }

 private:
   std::unique_ptr<FILE, FileDeleter> file_;
};

/**
 * SafeDatabaseWrapper - RAII wrapper for sqlite3*
 */
class SafeDatabaseWrapper
{
 public:
   SafeDatabaseWrapper(const std::string &dbpath)
   {
      sqlite3 *raw = nullptr;
      int result = sqlite3_open(dbpath.c_str(), &raw);

      if (result != SQLITE_OK)
      {
         throw std::runtime_error("Failed to open DB");
      }

      std::cout << "[SafeDatabaseWrapper] Opened: " << dbpath << "\n";
      db_.reset(raw); // transfer ownership into unique_ptr
   }

   SafeDatabaseWrapper(const SafeDatabaseWrapper &) = delete;
   SafeDatabaseWrapper &operator=(const SafeDatabaseWrapper &) = delete;

   sqlite3 *get() const
   {
      return db_.get();
   }

   int execute(const std::string &sql)
   {
      char *errorMsg = nullptr;
      int result = sqlite3_exec(db_.get(), sql.c_str(), nullptr, nullptr, &errorMsg);
      if (result != SQLITE_OK)
      {
         std::string error = errorMsg ? errorMsg : "Unknown error";
         sqlite3_free(errorMsg);
         throw std::runtime_error("SQL Error: " + error);
      }
      return result;
   }

   int executeWithCallback(const std::string &sql, int (*callback)(void *, int, char **, char **),
                           void *callbackData)
   {
      char *errorMsg = nullptr;
      int result = sqlite3_exec(db_.get(), sql.c_str(), callback, callbackData, &errorMsg);
      if (result != SQLITE_OK)
      {
         std::string error = errorMsg ? errorMsg : "Unknown error";
         sqlite3_free(errorMsg);
         throw std::runtime_error("SQL Error: " + error);
      }
      return result;
   }

 private:
   std::unique_ptr<sqlite3, SqliteDeleter> db_;
};

// ============================================================================
// SECTION 4: Cleanup Verification Tests
// ============================================================================

/**
 * Tests cleanup on normal exit path
 */
void testNormalExit()
{
   std::cout << "\n=== Test: Normal Exit Path ===\n";
   auto file =
       std::unique_ptr<FILE, FileDeleter>(std::fopen("test_normal.txt", "w"), FileDeleter{});
   if (file)
   {
      std::fputs("Normal exit test\n", file.get());
   }
   std::cout << "Function ending normally - destructor will close file\n";
   // File automatically closed when 'file' goes out of scope
}

/**
 * Tests cleanup on early return
 */
void testEarlyReturn(bool shouldReturnEarly)
{
   std::cout << "\n=== Test: Early Return Path ===\n";
   auto file = std::unique_ptr<FILE, FileDeleter>(std::fopen("test_early.txt", "w"), FileDeleter{});

   if (shouldReturnEarly)
   {
      std::cout << "Early return - file still closed via destructor\n";
      return; // unique_ptr destructor called automatically
   }

   std::fputs("Completed normally\n", file.get());
   std::cout << "Normal completion - destructor closes file\n";
   // File closed here
}

/**
 * Tests cleanup when exception is thrown
 */
void testExceptionThrown(bool shouldThrow)
{
   std::cout << "\n=== Test: Exception Path ===\n";

   auto file =
       std::unique_ptr<FILE, FileDeleter>(std::fopen("test_exception.txt", "w"), FileDeleter{});

   if (shouldThrow)
   {
      std::cout << "Exception about to be thrown - file closed via stack unwinding\n";
      throw std::runtime_error("Simulated exception - resource still cleaned up!");
   }

   std::fputs("No exception - file closed normally\n", file.get());
   std::cout << "Normal path - file closed\n";
}

/**
 * Tests cleanup with nested scopes and multiple resources
 */
void testNestedScopes()
{
   std::cout << "\n=== Test: Nested Scopes ===\n";

   {
      auto outer =
          std::unique_ptr<FILE, FileDeleter>(std::fopen("test_outer.txt", "w"), FileDeleter{});
      std::fputs("Outer file opened\n", outer.get());

      {
         auto inner =
             std::unique_ptr<FILE, FileDeleter>(std::fopen("test_inner.txt", "w"), FileDeleter{});
         std::fputs("Inner file opened\n", inner.get());
         std::cout << "Inner scope ending\n";
         // Inner file closed here
      }

      std::fputs("Back to outer scope\n", outer.get());
      std::cout << "Outer scope ending\n";
      // Outer file closed here
   }
   std::cout << "All nested resources cleaned up\n";
}

/**
 * Tests with wrapper classes for cleaner usage
 */
void testWrapperClasses()
{
   std::cout << "\n=== Test: Wrapper Classes ===\n";

   try
   {
      SafeFileWrapper file("test_wrapper.txt", "w");
      file.write("Wrapper test successful\n");
      std::cout << "File operations completed\n";
      // File closed automatically at scope end
   }
   catch (const std::exception &e)
   {
      std::cerr << "Exception: " << e.what() << "\n";
   }

   std::cout << "Wrapper class cleaned up properly\n";
}

/**
 * Tests exception safety with multiple resources
 */
void testMultiResourceException()
{
   std::cout << "\n=== Test: Multiple Resources + Exception ===\n";

   std::unique_ptr<FILE, FileDeleter> file1(std::fopen("res1.txt", "w"), FileDeleter{});
   std::unique_ptr<FILE, FileDeleter> file2(std::fopen("res2.txt", "w"), FileDeleter{});
   std::unique_ptr<FILE, FileDeleter> file3(std::fopen("res3.txt", "w"), FileDeleter{});

   std::fputs("All resources acquired\n", file1.get());
   std::fputs("All resources acquired\n", file2.get());
   std::fputs("All resources acquired\n", file3.get());

   std::cout << "Throwing exception - all 3 files will be closed\n";
   throw std::runtime_error("Multiple resource cleanup test");
   // All three unique_ptr destructors called during stack unwinding
}

// ============================================================================
// SECTION 5: Main - Run All Tests
// ============================================================================

int main()
{
   std::cout << "========================================\n";
   std::cout << "C++ Resource Management with unique_ptr\n";
   std::cout << "========================================\n";

   // Test 1: Normal exit
   testNormalExit();

   // Test 2: Early return (non-early first)
   testEarlyReturn(false);
   testEarlyReturn(true); // This one returns early

   // Test 3: Exception handling
   try
   {
      testExceptionThrown(false);
      testExceptionThrown(true); // Throws exception
   }
   catch (const std::exception &e)
   {
      std::cout << "[Caught] " << e.what() << "\n";
   }

   // Test 4: Nested scopes
   testNestedScopes();

   // Test 5: Wrapper classes
   testWrapperClasses();

   // Test 6: Multiple resources with exception
   try
   {
      testMultiResourceException();
   }
   catch (const std::exception &e)
   {
      std::cout << "[Caught] " << e.what() << "\n";
   }

   std::cout << "\n========================================\n";
   std::cout << "All tests completed successfully!\n";
   std::cout << "All resources cleaned up via RAII\n";
   std::cout << "========================================\n";

   return 0;
}
