import os

# Initialize counters
success_count = 0
fail_count = 0

def path_join(*paths):
    """Join one or more path components intelligently."""
    return '/'.join(paths).replace('//', '/')

def path_exists(path):
    """Check if a path exists."""
    try:
        os.stat(path)
        return True
    except OSError:
        return False

def test_environment(path):
    """Set up the environment for testing by ensuring each part of the path exists.
    If the final directory exists, the test fails. Otherwise, it is created."""

    # Split the path into components
    path_components = path.strip("/").split("/")

    current_path = "/"

    for component in path_components[:-1]:
        current_path = path_join(current_path, component)

        if not path_exists(current_path):
            os.mkdir(current_path)
            # print(f"Directory '{current_path}' does not exist. Created directory.")

    # The final directory should not exist
    final_dir = path_join(current_path, path_components[-1])

    if path_exists(final_dir):
        raise AssertionError(f"Test failed: Directory '{final_dir}' already exists.")
    else:
        os.mkdir(final_dir)
        print(f"Test environment set up: '{final_dir}' created successfully.")

    return final_dir

def simple_write_and_read_back_test(directory):
    """Test writing to a file and reading it back to verify contents."""
    global success_count, fail_count

    test_file = path_join(directory, "test_file.txt")
    test_content = "Hello, this is a test file."

    try:
        # Write content to the file
        with open(test_file, "w") as f:
            f.write(test_content)
        # print(f"File '{test_file}' written successfully.")

        # Read the content back
        with open(test_file, "r") as f:
            read_content = f.read()

        # Verify the content
        assert read_content == test_content, f"Test failed: Content mismatch. Expected: '{test_content}', Got: '{read_content}'"
        # print(f"File '{test_file}' read successfully, and content verified.")

        # Increment success count
        success_count += 1

    except AssertionError as e:
        print(e)
        fail_count += 1

    finally:
        # Cleanup
        if path_exists(test_file):
            os.remove(test_file)
            # print(f"File '{test_file}' removed after test.")

def test_write_and_read_back_complex(directory):
    """Test writing a poem line by line to a file and reading it back to verify contents."""
    global success_count, fail_count

    test_file = path_join(directory, "test_poem.txt")
    poem_lines = [
        "Two roads diverged in a yellow wood,",
        "And sorry I could not travel both",
        "And be one traveler, long I stood",
        "And looked down one as far as I could",
        "To where it bent in the undergrowth;",
        "",
        "Then took the other, as just as fair,",
        "And having perhaps the better claim,",
        "Because it was grassy and wanted wear;",
        "Though as for that the passing there",
        "Had worn them really about the same,",
        "",
        "And both that morning equally lay",
        "In leaves no step had trodden black.",
        "Oh, I kept the first for another day!",
        "Yet knowing how way leads on to way,",
        "I doubted if I should ever come back.",
        "",
        "I shall be telling this with a sigh",
        "Somewhere ages and ages hence:",
        "Two roads diverged in a wood, and I—",
        "I took the one less traveled by,",
        "And that has made all the difference."
    ]

    try:
        # Write the poem to the file line by line and read back each line to verify
        with open(test_file, "w") as f:
            for index, line in enumerate(poem_lines):
                f.write(line + "\n")
                # print(f"Line {index + 1} written to file.")

                # Read the line back immediately to verify
                f.flush()  # Ensure the content is written to the file
                with open(test_file, "r") as fr:
                    lines = fr.readlines()
                    read_back_line = lines[-1].strip()  # Read the last line written

                    assert read_back_line == line, (
                        f"Test failed at line {index + 1}: Expected: '{line}', Got: '{read_back_line}'"
                    )

        # Increment success count if all lines are verified
        success_count += 1

    except AssertionError as e:
        print(e)
        fail_count += 1

    finally:
        # Cleanup
        if path_exists(test_file):
            os.remove(test_file)

def test_mkdir_and_remove(directory):
    """Test creating directories with various names and removing them."""
    global success_count, fail_count

    # List of directory names to test
    dir_names = [
        "test_dir_1",
        "Test_Dir_2",
        "test-dir-3",
        "test.dir.4",
        "test_dir_with_a_very_long_name_that_should_still_work",
        "123_test_dir",
        "!@#_test_dir_special_chars",
        "dir with spaces",
        "unicode_测试_目录",
        "mixedCASE_TestDir"
    ]

    try:
        # Create directories and verify creation
        for dir_name in dir_names:
            test_dir = path_join(directory, dir_name)
            os.mkdir(test_dir)

            # Verify the directory was created
            assert path_exists(test_dir), f"Test failed: Directory '{test_dir}' was not created."

        # Remove directories and verify removal
        for dir_name in dir_names:
            test_dir = path_join(directory, dir_name)
            os.rmdir(test_dir)

            # Verify the directory was removed
            assert not path_exists(test_dir), f"Test failed: Directory '{test_dir}' was not removed."

        # Increment success count if all directories are verified
        success_count += 1

    except AssertionError as e:
        print(e)
        fail_count += 1

def generate_large_content(size_in_mb=1):
    """Generate a string that is approximately `size_in_mb` megabytes."""
    base_text = (
        "It is a truth universally acknowledged, that a single man in possession of a good fortune, "
        "must be in want of a wife. However little known the feelings or views of such a man may be "
        "on his first entering a neighbourhood, this truth is so well fixed in the minds of the "
        "surrounding families, that he is considered as the rightful property of some one or other "
        "of their daughters.\n"
        "My dear Mr. Bennet,\" said his lady to him one day, \"have you heard that Netherfield Park is let at last?\"\n"
        "\"Mr. Bennet replied that he had not.\n"
        "\"But it is,\" returned she; \"for Mrs. Long has just been here, and she told me all about it.\"\n"
        "\"Mr. Bennet made no answer.\n"
        "\"Do not you want to know who has taken it?\" cried his wife impatiently.\n"
        "\"You want to tell me, and I have no objection to hearing it.\"\n"
        "\"This was invitation enough.\n"
        "\"Why, my dear, you must know, Mrs. Long says that Netherfield is taken by a young man of large fortune "
        "from the north of England; that he came down on Monday in a chaise and four to see the place, "
        "and was so much delighted with it that he agreed with Mr. Morris immediately; that he is to take "
        "possession before Michaelmas, and some of his servants are to be in the house by the end of next week.\"\n"
    )
    repeat_count = (size_in_mb * 1024 * 1024) // len(base_text)
    return base_text * repeat_count

def test_big_file_write_and_read(directory):
    """Test writing and reading a large file to verify the content."""
    global success_count, fail_count

    test_file = path_join(directory, "test_big_file.txt")
    # It seems that the size_in_mb being set to too high could break the qemu blk_driver
    large_content = generate_large_content(size_in_mb=1)  # Generate 1 MB of content

    try:
        # Write the large content to the file
        with open(test_file, "w") as f:
            f.write(large_content)

        # Read the content back
        with open(test_file, "r") as f:
            read_content = f.read()

        # Verify the content
        assert read_content == large_content, "Test failed: The content of the large file does not match."

        # Increment success count
        success_count += 1

    except AssertionError as e:
        print(e)
        fail_count += 1

    finally:
        # Cleanup
        if path_exists(test_file):
            os.remove(test_file)

def test_rename_files_and_dirs(directory):
    """Test renaming directories and files after creating and writing content to them."""
    global success_count, fail_count

    # Names for directories and files
    original_dir_name = "original_dir"
    renamed_dir_name = "renamed_dir"
    original_file_name = "original_file.txt"
    renamed_file_name = "renamed_file.txt"

    try:
        # Create the original directory
        original_dir_path = path_join(directory, original_dir_name)
        os.mkdir(original_dir_path)

        # Create a file in the original directory and write content to it
        original_file_path = path_join(original_dir_path, original_file_name)
        file_content = "This is a test file for renaming."

        with open(original_file_path, "w") as f:
            f.write(file_content)

        # Rename the file
        renamed_file_path = path_join(original_dir_path, renamed_file_name)
        os.rename(original_file_path, renamed_file_path)

        # Verify the renamed file exists and contains the correct content
        assert path_exists(renamed_file_path), f"Test failed: Renamed file '{renamed_file_path}' does not exist."
        with open(renamed_file_path, "r") as f:
            read_content = f.read()
        assert read_content == file_content, "Test failed: Renamed file content does not match."

        # Rename the directory
        renamed_dir_path = path_join(directory, renamed_dir_name)
        os.rename(original_dir_path, renamed_dir_path)

        # Verify the renamed directory exists and contains the renamed file
        assert path_exists(renamed_dir_path), f"Test failed: Renamed directory '{renamed_dir_path}' does not exist."
        assert path_exists(path_join(renamed_dir_path, renamed_file_name)), (
            f"Test failed: File '{renamed_file_name}' does not exist in renamed directory '{renamed_dir_path}'."
        )

        # Increment success count
        success_count += 1

    except AssertionError as e:
        print(e)
        fail_count += 1

    finally:
        # Cleanup: Remove the renamed file and directory
        renamed_file_path = path_join(renamed_dir_path, renamed_file_name)
        if path_exists(renamed_file_path):
            os.remove(renamed_file_path)
        if path_exists(renamed_dir_path):
            os.rmdir(renamed_dir_path)

def test_truncate_file(directory):
    """Test truncating a file and verifying the truncated content."""
    global success_count, fail_count

    test_file = path_join(directory, "test_truncate.txt")
    poem = (
        "I met a traveler from an antique land\n"
        "Who said: Two vast and trunkless legs of stone\n"
        "Stand in the desert. Near them, on the sand,\n"
        "Half sunk, a shattered visage lies, whose frown,\n"
        "And wrinkled lip, and sneer of cold command,\n"
        "Tell that its sculptor well those passions read\n"
        "Which yet survive, stamped on these lifeless things,\n"
        "The hand that mocked them and the heart that fed.\n"
        "And on the pedestal, these words appear:\n"
        "My name is Ozymandias, King of Kings;\n"
        "Look on my Works, ye Mighty, and despair!\n"
        "Nothing beside remains. Round the decay\n"
        "Of that colossal Wreck, boundless and bare\n"
        "The lone and level sands stretch far away."
    )

    try:
        # Write the poem to the file
        with open(test_file, "w") as f:
            f.write(poem)

        # Truncate the file in steps and verify the content
        truncation_lengths = [200, 150, 100, 50, 0]  # Example truncation lengths

        for length in truncation_lengths:
            with open(test_file, "r+") as f:
                f.truncate(length)
                f.seek(0)
                truncated_content = f.read()

            expected_content = poem[:length]
            assert truncated_content == expected_content, (
                f"Test failed: Truncated content does not match expected at length {length}. ",
                f"Expected: {repr(expected_content)}, Got: {repr(truncated_content)}"
            )

        # Increment success count if all truncations are verified
        success_count += 1

    except AssertionError as e:
        print(e)
        fail_count += 1

    finally:
        # Cleanup: Remove the test file
        if path_exists(test_file):
            os.remove(test_file)

def run_tests():
    test_dir_path = "/home/li/test_dir"

    # Run the test environment setup
    test_environment(test_dir_path)

    # Indicate the start of the test
    print("Test 1: Running test_write_and_read_back")

    # Run the write and read back test
    simple_write_and_read_back_test(test_dir_path)

    # Indicate the start of the second, more complex test
    print("\nTest 2: Running test_write_and_read_back_complex")

    test_write_and_read_back_complex(test_dir_path)

    # Indicate the start of the third test
    print("\nTest 3: Running test_mkdir_and_remove")

    test_mkdir_and_remove(test_dir_path)

    # Indicate the start of the fourth test
    print("\nTest 4: Running test_big_file_write_and_read")

    test_big_file_write_and_read(test_dir_path)

    # Indicate the start of the fifth test
    print("\nTest 5: Running test_rename_files_and_dirs")

    test_rename_files_and_dirs(test_dir_path)

    # Indicate the start of the sixth test
    print("\nTest 6: Running test_truncate_file")

    test_truncate_file(test_dir_path)

    # Print the results
    print(f"\nTests completed. Success: {success_count}, Fail: {fail_count}")

    # Cleanup: Remove the test dir
    if path_exists(test_dir_path):
        os.rmdir(test_dir_path)
