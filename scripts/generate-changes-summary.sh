#!/bin/bash
# Generate a summary of code changes since the last release
# This script can be used to help prepare release notes

set -e

SCRIPT_DIR=$(dirname "$0")
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
cd "$REPO_ROOT"

# Colors for output (if terminal supports it)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    BLUE='\033[0;34m'
    MAGENTA='\033[0;35m'
    CYAN='\033[0;36m'
    BOLD='\033[1m'
    NC='\033[0m' # No Color
else
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    MAGENTA=''
    CYAN=''
    BOLD=''
    NC=''
fi

# Function to print colored output
print_header() {
    echo "${BOLD}${BLUE}=== $1 ===${NC}"
}

print_section() {
    echo "${BOLD}${CYAN}## $1${NC}"
}

print_error() {
    echo "${RED}ERROR: $1${NC}" >&2
}

# Check if we're in a git repository
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    print_error "Not in a git repository"
    exit 1
fi

# Parse command line arguments
OUTPUT_FORMAT="text"
SHOW_STATS=0
SHOW_AUTHORS=0
GROUP_BY_TYPE=1
FROM_REF=""

while [ $# -gt 0 ]; do
    case "$1" in
        --format=*)
            OUTPUT_FORMAT="${1#*=}"
            shift
            ;;
        --from=*)
            FROM_REF="${1#*=}"
            shift
            ;;
        --stats)
            SHOW_STATS=1
            shift
            ;;
        --authors)
            SHOW_AUTHORS=1
            shift
            ;;
        --no-grouping)
            GROUP_BY_TYPE=0
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Generate a summary of code changes since the last release"
            echo ""
            echo "Options:"
            echo "  --format=FORMAT    Output format: text, markdown (default: text)"
            echo "  --from=REF         Compare from specific tag/commit (default: auto-detect latest tag)"
            echo "  --stats            Show commit statistics"
            echo "  --authors          Show list of authors"
            echo "  --no-grouping      Don't group commits by type"
            echo "  --help, -h         Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0"
            echo "  $0 --format=markdown --stats --authors"
            echo "  $0 --from=v0.44.0"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Disable colors for markdown output
if [ "$OUTPUT_FORMAT" = "markdown" ]; then
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    MAGENTA=''
    CYAN=''
    BOLD=''
    NC=''
fi

# Find the latest release tag if not specified
if [ -z "$FROM_REF" ]; then
    # Try to find the latest tag that is an ancestor of HEAD
    LATEST_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "")
    
    if [ -z "$LATEST_TAG" ]; then
        # If that fails, try to find the latest tag by version sorting
        LATEST_TAG=$(git tag -l 'v*' --sort=-version:refname 2>/dev/null | head -1)
        
        if [ -z "$LATEST_TAG" ]; then
            # If no tags exist at all, use the first commit
            LATEST_TAG=$(git rev-list --max-parents=0 HEAD 2>/dev/null || echo "HEAD~10")
            print_error "No release tags found, using: $LATEST_TAG"
        fi
    fi
    FROM_REF="$LATEST_TAG"
fi

# Verify the reference exists
if ! git rev-parse "$FROM_REF" >/dev/null 2>&1; then
    print_error "Reference '$FROM_REF' not found"
    exit 1
fi

# Get the date of the reference
TAG_DATE=$(git log -1 --format=%ai "$FROM_REF" 2>/dev/null | cut -d' ' -f1)
if [ -z "$TAG_DATE" ]; then
    TAG_DATE="(unknown)"
fi

# Get commit count since reference
COMMIT_COUNT=$(git rev-list ${FROM_REF}..HEAD --count)

if [ "$COMMIT_COUNT" -eq 0 ]; then
    print_header "No changes since reference"
    echo "Reference: ${FROM_REF} (${TAG_DATE})"
    exit 0
fi

# Print header based on format
if [ "$OUTPUT_FORMAT" = "markdown" ]; then
    echo "# Changes Since Last Release"
    echo ""
    echo "**From:** \`${FROM_REF}\` (${TAG_DATE})"
    echo "**Commits:** ${COMMIT_COUNT}"
    echo ""
else
    print_header "Changes Since Last Release"
    echo "From: ${FROM_REF} (${TAG_DATE})"
    echo "Commits: ${COMMIT_COUNT}"
    echo ""
fi

# Function to categorize commit by type
categorize_commit() {
    local msg="$1"
    local type=""
    
    # Check conventional commit format
    # Match patterns more precisely: type: or type(scope):
    case "$msg" in
        feat:*|feat\(*\):*) type="Features" ;;
        fix:*|fix\(*\):*|Fix:*|Fixed*) type="Bug Fixes" ;;
        docs:*|docs\(*\):*|doc:*) type="Documentation" ;;
        chore:*|chore\(*\):*) type="Chores" ;;
        test:*|tests:*) type="Tests" ;;
        refactor:*|refactor\(*\):*) type="Refactoring" ;;
        perf:*|perf\(*\):*) type="Performance" ;;
        style:*|style\(*\):*) type="Style" ;;
        build:*|build\(*\):*|ci:*|ci\(*\):*) type="Build/CI" ;;
        Merge\ pull\ request*) type="Merges" ;;
        *) type="Other Changes" ;;
    esac
    
    # Sanitize the type to prevent directory traversal
    type=$(echo "$type" | tr '/' '_' | tr -d '.')
    
    echo "$type"
}

# Get commits with details
if [ "$GROUP_BY_TYPE" -eq 1 ]; then
    # Group commits by type
    print_section "Changes by Category"
    echo ""
    
    # Create temporary files for each category
    TMPDIR=$(mktemp -d)
    trap "rm -rf $TMPDIR" EXIT
    
    # Get all commits to a temp file first
    COMMITS_FILE="$TMPDIR/commits.txt"
    git log ${FROM_REF}..HEAD --pretty=format:"%h|%s|%an|%ae|%ad" --date=short > "$COMMITS_FILE"
    # Add a newline to ensure the last line is read
    echo "" >> "$COMMITS_FILE"
    
    # Process all commits
    while IFS='|' read -r hash subject author email date; do
        # Skip empty lines
        [ -z "$hash" ] && continue
        category=$(categorize_commit "$subject")
        echo "$hash|$subject|$author|$email|$date" >> "$TMPDIR/${category}.txt"
    done < "$COMMITS_FILE"
    
    # Display commits by category
    for category in "Features" "Bug Fixes" "Performance" "Refactoring" "Documentation" "Tests" "Build/CI" "Chores" "Merges" "Other Changes"; do
        if [ -f "$TMPDIR/${category}.txt" ]; then
            count=$(wc -l < "$TMPDIR/${category}.txt")
            
            if [ "$OUTPUT_FORMAT" = "markdown" ]; then
                echo "### $category ($count)"
                echo ""
                while IFS='|' read -r hash subject author email date; do
                    # Clean up the subject line - remove conventional commit prefixes
                    # Match: type: or type(scope): at the start of the line
                    clean_subject=$(echo "$subject" | sed -E 's/^[a-zA-Z]+: //' | sed -E 's/^[a-zA-Z]+\([^)]+\): //')
                    echo "* $clean_subject (\`$hash\`) - $author, $date"
                done < "$TMPDIR/${category}.txt"
                echo ""
            else
                echo "${BOLD}${category} (${count}):${NC}"
                while IFS='|' read -r hash subject author email date; do
                    echo "  ${GREEN}${hash}${NC} - $subject"
                    echo "         by $author on $date"
                done < "$TMPDIR/${category}.txt"
                echo ""
            fi
        fi
    done
else
    # Show commits in chronological order
    print_section "All Changes (Chronological)"
    echo ""
    
    if [ "$OUTPUT_FORMAT" = "markdown" ]; then
        git log ${FROM_REF}..HEAD --pretty=format:"* %s (\`%h\`) - %an, %ad" --date=short
        echo ""
    else
        git log ${FROM_REF}..HEAD --pretty=format:"  ${GREEN}%h${NC} - %s%n         by %an on %ad%n" --date=short
    fi
fi

# Show statistics if requested
if [ "$SHOW_STATS" -eq 1 ]; then
    echo ""
    print_section "Statistics"
    echo ""
    
    # Get git diff stats once and store in variable
    DIFF_STATS=$(git diff --stat ${FROM_REF}..HEAD)
    
    # Extract only the file lines (not the summary line)
    FILE_STATS=$(echo "$DIFF_STATS" | head -n -1)
    
    if [ "$OUTPUT_FORMAT" = "markdown" ]; then
        echo "#### Files Changed"
        echo "\`\`\`"
        echo "$DIFF_STATS" | tail -1
        echo "\`\`\`"
        echo ""
        echo "#### Most Modified Files"
        echo "\`\`\`"
        # Sort by the number after the pipe (changes count), using numeric sort
        echo "$FILE_STATS" | sort -t'|' -k2 -rn | head -10
        echo "\`\`\`"
    else
        echo "${BOLD}Files changed:${NC}"
        echo "$DIFF_STATS" | tail -1
        echo ""
        echo "${BOLD}Most modified files:${NC}"
        # Sort by the number after the pipe (changes count), using numeric sort
        echo "$FILE_STATS" | sort -t'|' -k2 -rn | head -10
    fi
    echo ""
fi

# Show authors if requested
if [ "$SHOW_AUTHORS" -eq 1 ]; then
    echo ""
    print_section "Contributors"
    echo ""
    
    # Get authors once and store in variable
    AUTHORS_LIST=$(git log ${FROM_REF}..HEAD --pretty=format:"%an <%ae>" | sort | uniq -c | sort -rn)
    
    if [ "$OUTPUT_FORMAT" = "markdown" ]; then
        echo "#### Authors (by commit count)"
        echo ""
        echo "$AUTHORS_LIST" | while read count name; do
            echo "* $name - $count commits"
        done
    else
        echo "${BOLD}Authors (by commit count):${NC}"
        echo "$AUTHORS_LIST" | while read count name; do
            echo "  $name - $count commits"
        done
    fi
    echo ""
fi

# Show summary footer
if [ "$OUTPUT_FORMAT" = "markdown" ]; then
    echo ""
    echo "---"
    echo "*Generated on $(date '+%Y-%m-%d %H:%M:%S')*"
else
    echo ""
    print_header "Summary Complete"
    echo "Generated on $(date '+%Y-%m-%d %H:%M:%S')"
fi
