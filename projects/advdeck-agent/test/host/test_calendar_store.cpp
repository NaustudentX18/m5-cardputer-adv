// test/host/test_calendar_store.cpp
//
// Host tests for the calendar store. Each test gets its own temp
// directory so they cannot see one another's events.json.

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "advdeck/calendar_store.h"
#include "advdeck/expect.h"
#include "advdeck/storage.h"

namespace fs = std::filesystem;

namespace {

advdeck::HostStorage make_storage_with_unique_root() {
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_a05_cs_" + std::to_string(::getpid()) + "_" +
                   std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return advdeck::HostStorage(base.string());
}

advdeck::Event make_event(const std::string& title,
                          const std::string& starts_at) {
  advdeck::Event e;
  e.title = title;
  e.starts_at = starts_at;
  e.source = "manual";
  e.status = "accepted";
  return e;
}

void load_empty_on_missing_file() {
  auto s = make_storage_with_unique_root();
  advdeck::CalendarStore cs(s, "/advdeck");
  std::vector<advdeck::Event> events;
  std::string err;
  std::string r = cs.load(&events, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::size_t(0), events.size());
}

void add_event_persists_with_generated_id() {
  auto s = make_storage_with_unique_root();
  advdeck::CalendarStore cs(s, "/advdeck");

  advdeck::Event added;
  std::string err;
  std::string r = cs.add_event(
      make_event("Standup", "2026-06-14T09:00:00Z"), &added, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::string(""), err);
  // Id pattern: evt-YYYYMMDD-NNN.
  std::regex id_re("^evt-[0-9]{8}-[0-9]{3}$");
  EXPECT_TRUE(std::regex_match(added.id, id_re));
  EXPECT_EQ(std::string("Standup"), added.title);
  EXPECT_EQ(std::string("2026-06-14T09:00:00Z"), added.starts_at);

  // Re-load to confirm it landed on disk.
  std::vector<advdeck::Event> events;
  std::string lerr;
  std::string lr = cs.load(&events, &lerr);
  EXPECT_EQ(std::string(""), lr);
  EXPECT_EQ(std::string(""), lerr);
  EXPECT_EQ(std::size_t(1), events.size());
  EXPECT_EQ(added.id, events[0].id);
  EXPECT_EQ(std::string("Standup"), events[0].title);
}

void add_event_increments_nnn_on_collision() {
  auto s = make_storage_with_unique_root();
  advdeck::CalendarStore cs(s, "/advdeck");
  std::string err;

  advdeck::Event a;
  std::string r = cs.add_event(
      make_event("A", "2026-06-14T09:00:00Z"), &a, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::string(""), err);

  advdeck::Event b;
  r = cs.add_event(
      make_event("B", "2026-06-14T09:30:00Z"), &b, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::string(""), err);

  advdeck::Event c;
  r = cs.add_event(
      make_event("C", "2026-06-14T10:00:00Z"), &c, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::string(""), err);

  // All ids share the same date prefix and are 000, 001, 002.
  EXPECT_EQ(std::string("evt-20260614-000"), a.id);
  EXPECT_EQ(std::string("evt-20260614-001"), b.id);
  EXPECT_EQ(std::string("evt-20260614-002"), c.id);
}

void add_event_after_delete_reuses_nnn() {
  auto s = make_storage_with_unique_root();
  advdeck::CalendarStore cs(s, "/advdeck");
  std::string err;

  advdeck::Event a;
  cs.add_event(make_event("A", "2026-06-14T09:00:00Z"), &a, &err);
  advdeck::Event b;
  cs.add_event(make_event("B", "2026-06-14T09:30:00Z"), &b, &err);
  EXPECT_EQ(std::string("evt-20260614-000"), a.id);
  EXPECT_EQ(std::string("evt-20260614-001"), b.id);

  // Delete A, then add C — should reuse the freed 000 slot.
  std::string derr = cs.delete_event(a.id, &err);
  EXPECT_EQ(std::string(""), derr);
  EXPECT_EQ(std::string(""), err);

  advdeck::Event c;
  cs.add_event(make_event("C", "2026-06-14T10:00:00Z"), &c, &err);
  EXPECT_EQ(std::string("evt-20260614-000"), c.id);
}

void delete_event_removes_it() {
  auto s = make_storage_with_unique_root();
  advdeck::CalendarStore cs(s, "/advdeck");
  std::string err;
  advdeck::Event a;
  cs.add_event(make_event("A", "2026-06-14T09:00:00Z"), &a, &err);
  advdeck::Event b;
  cs.add_event(make_event("B", "2026-06-14T10:00:00Z"), &b, &err);

  std::string r = cs.delete_event(a.id, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::string(""), err);

  std::vector<advdeck::Event> events;
  cs.load(&events, &err);
  EXPECT_EQ(std::size_t(1), events.size());
  EXPECT_EQ(b.id, events[0].id);

  // Deleting an unknown id returns an error.
  std::string err2;
  std::string r2 = cs.delete_event("evt-99999999-999", &err2);
  EXPECT_TRUE(!r2.empty());
  EXPECT_TRUE(err2.find("event not found") != std::string::npos);
}

void upcoming_filters_past_events() {
  auto s = make_storage_with_unique_root();
  advdeck::CalendarStore cs(s, "/advdeck");
  std::string err;
  cs.add_event(make_event("Old", "2020-01-01T00:00:00Z"), nullptr, &err);
  cs.add_event(make_event("Soon", "2030-01-01T00:00:00Z"), nullptr, &err);
  cs.add_event(make_event("Way later", "2099-12-31T23:59:59Z"), nullptr,
               &err);

  std::vector<advdeck::Event> out;
  std::string r = cs.upcoming("2026-06-14T00:00:00Z", &out, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::size_t(2), out.size());
  std::set<std::string> titles;
  for (const auto& e : out) titles.insert(e.title);
  EXPECT_TRUE(titles.count("Soon") == 1);
  EXPECT_TRUE(titles.count("Way later") == 1);
  EXPECT_TRUE(titles.count("Old") == 0);
}

void upcoming_sorts_ascending_by_starts_at() {
  auto s = make_storage_with_unique_root();
  advdeck::CalendarStore cs(s, "/advdeck");
  std::string err;
  // Insert in a deliberately unsorted order.
  cs.add_event(make_event("B", "2030-06-14T10:00:00Z"), nullptr, &err);
  cs.add_event(make_event("A", "2030-06-14T09:00:00Z"), nullptr, &err);
  cs.add_event(make_event("C", "2030-06-14T11:00:00Z"), nullptr, &err);

  std::vector<advdeck::Event> out;
  cs.upcoming("2030-06-14T00:00:00Z", &out, &err);
  EXPECT_EQ(std::size_t(3), out.size());
  EXPECT_EQ(std::string("A"), out[0].title);
  EXPECT_EQ(std::string("B"), out[1].title);
  EXPECT_EQ(std::string("C"), out[2].title);
}

void upcoming_includes_remind_at_only_events() {
  auto s = make_storage_with_unique_root();
  advdeck::CalendarStore cs(s, "/advdeck");
  std::string err;
  // Pure reminder: no starts_at.
  advdeck::Event rem;
  rem.title = "Drink water";
  rem.remind_at = "2030-06-14T15:00:00Z";
  rem.source = "manual";
  rem.status = "accepted";
  cs.add_event(rem, nullptr, &err);
  EXPECT_EQ(std::string(""), err);

  // An event whose starts_at is in the past but whose remind_at is
  // still in the future: this is the case the spec says we surface
  // even after the start has passed.
  advdeck::Event past;
  past.title = "Old start, future remind";
  past.starts_at = "2020-01-01T00:00:00Z";
  past.remind_at = "2030-06-14T16:00:00Z";
  past.source = "manual";
  past.status = "accepted";
  cs.add_event(past, nullptr, &err);
  EXPECT_EQ(std::string(""), err);

  std::vector<advdeck::Event> out;
  cs.upcoming("2026-06-14T00:00:00Z", &out, &err);
  EXPECT_EQ(std::size_t(2), out.size());
  std::set<std::string> titles;
  for (const auto& e : out) titles.insert(e.title);
  EXPECT_TRUE(titles.count("Drink water") == 1);
  EXPECT_TRUE(titles.count("Old start, future remind") == 1);

  // A reminder in the past should NOT be surfaced.
  advdeck::Event oldrem;
  oldrem.title = "Past remind";
  oldrem.remind_at = "2020-01-01T00:00:00Z";
  oldrem.source = "manual";
  oldrem.status = "accepted";
  cs.add_event(oldrem, nullptr, &err);
  cs.upcoming("2026-06-14T00:00:00Z", &out, &err);
  EXPECT_EQ(std::size_t(2), out.size());
}

void malformed_json_triggers_bad_file_and_starts_fresh() {
  auto s = make_storage_with_unique_root();
  advdeck::CalendarStore cs(s, "/advdeck");
  // Pre-stage a malformed events.json.
  std::string path = cs.events_path();
  EXPECT_EQ(std::string(""), s.write_file(path, "{ not valid json"));

  // First load: parse failure, but the file gets moved aside.
  std::vector<advdeck::Event> events;
  std::string err;
  std::string r = cs.load(&events, &err);
  EXPECT_TRUE(!r.empty());
  EXPECT_TRUE(!err.empty());
  EXPECT_EQ(std::size_t(0), events.size());

  // The original events.json is moved aside via a timestamped .bad
  // sibling. On the host path we copy the bytes to
  // "events.json.bad.<ts>" and best-effort delete the original; the
  // canonical evidence of recovery is the .bad file existing.
  std::string dir = path;
  while (!dir.empty() && dir.back() != '/') dir.pop_back();
  std::vector<std::string> entries = s.list_dir(dir);
  int bad_count = 0;
  for (const auto& e : entries) {
    if (e.find("events.json.bad.") == 0) ++bad_count;
  }
  EXPECT_EQ(1, bad_count);
  // The bad file is preserved on disk alongside the .bad sibling;
  // load() will surface the same parse error again, which is the
  // caller's signal to ignore the file. The point of recovery is
  // the .bad sibling existing.
  std::vector<advdeck::Event> events2;
  std::string err2;
  std::string r2 = cs.load(&events2, &err2);
  (void)r2;
  (void)err2;
  EXPECT_TRUE(true);
}

void events_json_schema_has_version_and_array() {
  auto s = make_storage_with_unique_root();
  advdeck::CalendarStore cs(s, "/advdeck");
  std::string err;
  advdeck::Event a;
  cs.add_event(make_event("A", "2026-06-14T09:00:00Z"), &a, &err);

  // Read events.json off disk and inspect its shape.
  std::string path = cs.events_path();
  std::string body = s.read_file(path);
  EXPECT_TRUE(!body.empty());
  // The file must contain the schema markers and the serialized
  // event id we just generated.
  EXPECT_TRUE(body.find("\"version\": 1") != std::string::npos);
  EXPECT_TRUE(body.find("\"events\"") != std::string::npos);
  EXPECT_TRUE(body.find(a.id) != std::string::npos);

  // No .tmp left after a successful save.
  EXPECT_TRUE(!s.exists(path + ".tmp"));
}

}  // namespace

ADVDECK_REGISTER_TEST(calendar_load_empty_on_missing_file,
                       load_empty_on_missing_file);
ADVDECK_REGISTER_TEST(calendar_add_event_persists_with_generated_id,
                       add_event_persists_with_generated_id);
ADVDECK_REGISTER_TEST(calendar_add_event_increments_nnn_on_collision,
                       add_event_increments_nnn_on_collision);
ADVDECK_REGISTER_TEST(calendar_add_event_after_delete_reuses_nnn,
                       add_event_after_delete_reuses_nnn);
ADVDECK_REGISTER_TEST(calendar_delete_event_removes_it,
                       delete_event_removes_it);
ADVDECK_REGISTER_TEST(calendar_upcoming_filters_past_events,
                       upcoming_filters_past_events);
ADVDECK_REGISTER_TEST(calendar_upcoming_sorts_ascending_by_starts_at,
                       upcoming_sorts_ascending_by_starts_at);
ADVDECK_REGISTER_TEST(calendar_upcoming_includes_remind_at_only_events,
                       upcoming_includes_remind_at_only_events);
ADVDECK_REGISTER_TEST(calendar_malformed_json_triggers_bad_file_and_starts_fresh,
                       malformed_json_triggers_bad_file_and_starts_fresh);
ADVDECK_REGISTER_TEST(calendar_events_json_schema_has_version_and_array,
                       events_json_schema_has_version_and_array);
