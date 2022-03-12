# Concurrent-filesystem
The second project for the Concurrent Programming course in the winter semester of the 2021/22 academic year at the University of Warsaw.

## The project statement
We were tasked with creating a library called Tree, representing a filesystem and allowing creation, deletion and moving of directories, as well as listing the contents of a directory. Each operation had to return proper error codes, when a given path was invalid (EINVAL), when a given path led to a directory which didn't exist (ENOENT), when a directory to be created already existed (EEXIST), and when a directory could not be moved to its subdirectory (EBUSY).
