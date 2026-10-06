#pragma once

#include <stddef.h>
#include <stdint.h>

#include "student_model.h"

constexpr uint16_t DEMO_STUDENT_ID = 7;

const Student *getStudentById(uint16_t student_id);
const Student *getStudentAtIndex(size_t index);
const Student *getDemoStudent();
const AccountRecord *getAccountByStudentId(uint16_t student_id);
void setAccountBalance(uint16_t student_id, int32_t aureos);
const ReadingRecord *getReadingRecords(uint16_t student_id, size_t *count);
const WritingRecord *getWritingRecords(uint16_t student_id, size_t *count);
const StudentMovement *getStudentMovements(uint16_t student_id, size_t *count);
const AttendanceRecord *getAttendanceRecords(uint16_t student_id, size_t *count);

size_t getStudentCount();
size_t getReadingRecordCount();
size_t getWritingRecordCount();
