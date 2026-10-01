#include "provisional_data.h"

namespace {

constexpr Student students[] = {
    {1, nullptr, "GUTIERREZ PEREZ ALEXA VICTORIA", nullptr, 3, nullptr, 1, nullptr, nullptr},
    {2, nullptr, "HERNANDEZ MANJARREZ ALEXIA GUADALUPE", nullptr, 3, nullptr, 2, nullptr, nullptr},
    {3, nullptr, "OCHOA BALTAZAR ULISES ISAC", nullptr, 3, nullptr, 3, nullptr, nullptr},
    {4, nullptr, "OCHOA PANIAGUA BRIANA DALEYSA", nullptr, 3, nullptr, 4, nullptr, nullptr},
    {5, nullptr, "RODRIGUEZ MARIN ARIEL", nullptr, 3, nullptr, 5, nullptr, nullptr},
    {6, nullptr, "TIBURCIO FELIPE ANGEL DAVID", nullptr, 3, nullptr, 6, nullptr, nullptr},
    {7, nullptr, "CAMACHO ARREOLA ALONZO DARIO", "Dar\xC3\xAD" "o", 4, "B", 7, nullptr, "Explorador"},
    {8, nullptr, "DANIEL MORENO ABRAM SANTIAGO", nullptr, 4, nullptr, 8, nullptr, nullptr},
    {9, nullptr, "MEJIA GUTIERREZ MIGUEL ADRIAN", nullptr, 4, nullptr, 9, nullptr, nullptr},
    {10, nullptr, "OCHOA PEREZ MARLEN", nullptr, 4, nullptr, 10, nullptr, nullptr},
    {11, nullptr, "PEREZ VARGAS YULIANA ISABEL", nullptr, 4, nullptr, 11, nullptr, nullptr},
    {12, nullptr, "RODRIGUEZ COBIAN WILLIAM GUADALUPE", nullptr, 4, nullptr, 12, nullptr, nullptr},
    {13, nullptr, "RODRIGUEZ TALANCON NATALI FERNANDA", nullptr, 4, nullptr, 13, nullptr, nullptr}
};

AccountRecord accounts[] = {
    {DEMO_STUDENT_ID, 125}
};

constexpr ReadingRecord reading_records[] = {
    {1, "01/09/2026", 67, true}, {1, "07/09/2026", 74, true}, {1, "11/09/2026", 111, true},
    {2, "01/09/2026", 47, true}, {2, "07/09/2026", 0, false}, {2, "11/09/2026", 68, true},
    {3, "01/09/2026", 83, true}, {3, "07/09/2026", 89, true}, {3, "11/09/2026", 102, true},
    {4, "01/09/2026", 24, true}, {4, "07/09/2026", 31, true}, {4, "11/09/2026", 33, true},
    {5, "01/09/2026", 63, true}, {5, "07/09/2026", 75, true}, {5, "11/09/2026", 83, true},
    {6, "01/09/2026", 50, true}, {6, "07/09/2026", 65, true}, {6, "11/09/2026", 73, true},
    {7, "01/09/2026", 82, true}, {7, "07/09/2026", 83, true}, {7, "11/09/2026", 107, true},
    {8, "01/09/2026", 59, true}, {8, "07/09/2026", 65, true}, {8, "11/09/2026", 69, true},
    {9, "01/09/2026", 154, true}, {9, "07/09/2026", 132, true}, {9, "11/09/2026", 154, true},
    {10, "01/09/2026", 69, true}, {10, "07/09/2026", 76, true}, {10, "11/09/2026", 108, true},
    {11, "01/09/2026", 36, true}, {11, "07/09/2026", 63, true}, {11, "11/09/2026", 72, true},
    {12, "01/09/2026", 55, true}, {12, "07/09/2026", 70, true}, {12, "11/09/2026", 102, true},
    {13, "01/09/2026", 136, true}, {13, "07/09/2026", 108, true}, {13, "11/09/2026", 120, true}
};

constexpr WritingRecord writing_records[] = {
    {1, "01/09/2026", 34, 15, true}, {1, "07/09/2026", 45, 14, true}, {1, "11/09/2026", 0, 0, false}, {1, "21/09/2026", 58, 21, true},
    {2, "01/09/2026", 34, 14, true}, {2, "07/09/2026", 45, 0, false}, {2, "11/09/2026", 0, 0, false}, {2, "21/09/2026", 58, 26, true},
    {3, "01/09/2026", 34, 28, true}, {3, "07/09/2026", 45, 12, true}, {3, "11/09/2026", 0, 0, false}, {3, "21/09/2026", 0, 0, false},
    {4, "01/09/2026", 34, 31, true}, {4, "07/09/2026", 45, 15, true}, {4, "11/09/2026", 0, 0, false}, {4, "21/09/2026", 58, 31, true},
    {5, "01/09/2026", 34, 34, true}, {5, "07/09/2026", 45, 41, true}, {5, "11/09/2026", 0, 0, false}, {5, "21/09/2026", 58, 47, true},
    {6, "01/09/2026", 34, 34, true}, {6, "07/09/2026", 45, 30, true}, {6, "11/09/2026", 0, 0, false}, {6, "21/09/2026", 0, 0, false},
    {7, "01/09/2026", 34, 8, true}, {7, "07/09/2026", 45, 5, true}, {7, "11/09/2026", 0, 0, false}, {7, "21/09/2026", 58, 7, true},
    {8, "01/09/2026", 34, 20, true}, {8, "07/09/2026", 45, 18, true}, {8, "11/09/2026", 0, 0, false}, {8, "21/09/2026", 58, 29, true},
    {9, "01/09/2026", 34, 3, true}, {9, "07/09/2026", 45, 17, true}, {9, "11/09/2026", 0, 0, false}, {9, "21/09/2026", 58, 11, true},
    {10, "01/09/2026", 34, 4, true}, {10, "07/09/2026", 45, 8, true}, {10, "11/09/2026", 0, 0, false}, {10, "21/09/2026", 58, 10, true},
    {11, "01/09/2026", 34, 5, true}, {11, "07/09/2026", 45, 10, true}, {11, "11/09/2026", 0, 0, false}, {11, "21/09/2026", 58, 20, true},
    {12, "01/09/2026", 34, 4, true}, {12, "07/09/2026", 45, 8, true}, {12, "11/09/2026", 0, 0, false}, {12, "21/09/2026", 58, 10, true},
    {13, "01/09/2026", 34, 3, true}, {13, "07/09/2026", 45, 6, true}, {13, "11/09/2026", 0, 0, false}, {13, "21/09/2026", 58, 4, true}
};

static_assert(sizeof(students) / sizeof(students[0]) == 13, "Expected 13 provisional students");
static_assert(sizeof(reading_records) / sizeof(reading_records[0]) == 39, "Expected 39 reading records");
static_assert(sizeof(writing_records) / sizeof(writing_records[0]) == 52, "Expected 52 writing records");

constexpr StudentMovement movements[] = {
    {DEMO_STUDENT_ID, 10, MovementType::Credit, "Trabajo terminado", nullptr, MovementOrigin::BancoEscolar},
    {DEMO_STUDENT_ID, 5, MovementType::Credit, "Participaci\xC3\xB3" "n", nullptr, MovementOrigin::BancoEscolar},
    {DEMO_STUDENT_ID, -5, MovementType::Debit, "Movimiento de demostraci\xC3\xB3" "n", nullptr, MovementOrigin::BancoEscolar}
};

const AttendanceRecord attendance_records[] = {{0, nullptr, nullptr, nullptr}};

template <typename Record>
const Record *records_for_student(const Record *records, size_t record_count, uint16_t student_id, size_t *count)
{
    size_t first = record_count;
    size_t matched = 0;
    for (size_t i = 0; i < record_count; ++i) {
        if (records[i].student_id == student_id) {
            if (first == record_count) first = i;
            ++matched;
        }
    }
    if (count) *count = matched;
    return first == record_count ? nullptr : records + first;
}

} // namespace

const Student *getStudentById(uint16_t student_id)
{
    for (const Student &student : students) if (student.student_id == student_id) return &student;
    return nullptr;
}

const Student *getDemoStudent() { return getStudentById(DEMO_STUDENT_ID); }

const AccountRecord *getAccountByStudentId(uint16_t student_id)
{
    for (const AccountRecord &account : accounts) if (account.student_id == student_id) return &account;
    return nullptr;
}

void setAccountBalance(uint16_t student_id, int32_t aureos)
{
    for (AccountRecord &account : accounts) {
        if (account.student_id == student_id) {
            account.aureos = aureos;
            return;
        }
    }
}

const ReadingRecord *getReadingRecords(uint16_t student_id, size_t *count)
{
    return records_for_student(reading_records, sizeof(reading_records) / sizeof(reading_records[0]), student_id, count);
}

const WritingRecord *getWritingRecords(uint16_t student_id, size_t *count)
{
    return records_for_student(writing_records, sizeof(writing_records) / sizeof(writing_records[0]), student_id, count);
}

const StudentMovement *getStudentMovements(uint16_t student_id, size_t *count)
{
    return records_for_student(movements, sizeof(movements) / sizeof(movements[0]), student_id, count);
}

const AttendanceRecord *getAttendanceRecords(uint16_t student_id, size_t *count)
{
    (void)student_id;
    if (count) *count = 0;
    return attendance_records;
}

size_t getStudentCount() { return sizeof(students) / sizeof(students[0]); }
size_t getReadingRecordCount() { return sizeof(reading_records) / sizeof(reading_records[0]); }
size_t getWritingRecordCount() { return sizeof(writing_records) / sizeof(writing_records[0]); }
