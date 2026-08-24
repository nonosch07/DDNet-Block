// The vote menu is drawn with box-drawing runes and small-caps letters, both
// three bytes per character, into a description field that holds
// VOTE_DESC_LENGTH *bytes*. It is easy to build a line that looks short and is
// silently cut by the client, so this checks the geometry directly.

#include <base/str.h>

#include <game/voting.h>

#include <blockworlds/votes/votemanager.h>
#include <gtest/gtest.h>

#include <string>

static int Utf8Len(const std::string &Str)
{
	int Count = 0;
	for(char c : Str)
		if((static_cast<unsigned char>(c) & 0xC0) != 0x80)
			++Count;
	return Count;
}

class BwVoteBox : public ::testing::TestWithParam<const char *> // NOLINT(readability-identifier-naming)
{
};

TEST_P(BwVoteBox, BordersFitAndAlign)
{
	std::string Top, Bottom;
	CVoteManager::BuildBoxBorders(GetParam(), Top, Bottom);

	// must survive the wire: whole characters, inside the byte budget
	EXPECT_TRUE(str_utf8_check(Top.c_str())) << "header is not valid UTF-8: " << Top;
	EXPECT_TRUE(str_utf8_check(Bottom.c_str())) << "footer is not valid UTF-8: " << Bottom;
	EXPECT_LE((int)Top.size(), VOTE_DESC_LENGTH - 1)
		<< "header is " << Top.size() << " bytes, the client cuts it: " << Top;
	EXPECT_LE((int)Bottom.size(), VOTE_DESC_LENGTH - 1)
		<< "footer is " << Bottom.size() << " bytes, the client cuts it: " << Bottom;

	// and the box has to look like a box
	EXPECT_EQ(Utf8Len(Top), Utf8Len(Bottom))
		<< "header and footer are different widths, the box looks broken:\n  " << Top << "\n  " << Bottom;
}

INSTANTIATE_TEST_SUITE_P(Titles, BwVoteBox,
	::testing::Values(
		"Blockworlds Menu",
		"Server Votes",
		"Rules",
		"Leaderboards",
		"Shop - Skin Manipulations", // the longest one the menu builds
		"1on1 Config",
		"A ridiculously long page title that nobody would ever write"));

TEST(BwVoteBoxLabels, AnOptionLabelWithSmallCapsStillFits)
{
	// "│ " + 3 bytes per small-caps letter + " ›" -- about 19 letters is the max
	const std::string Label = "│ ꜱᴇʀᴠᴇʀ ᴠᴏᴛᴇꜱ ›";
	EXPECT_TRUE(str_utf8_check(Label.c_str()));
	EXPECT_LE((int)Label.size(), VOTE_DESC_LENGTH - 1) << Label.size() << " bytes";
}
