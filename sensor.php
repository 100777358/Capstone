<?php
header('Content-Type: text/plain'); // Set content type for easier parsing

$servername = "localhost";
$username = "root";
$password = "abc123";
$dbname = "test1";

$conn = new mysqli($servername, $username, $password, $dbname);

if ($conn->connect_error) {
    die("Connection failed: " . $conn->connect_error);
}

if ($_SERVER['REQUEST_METHOD'] == 'POST') {
    // Sanitize inputs to prevent SQL injection
    $user = $conn->real_escape_string($_POST['username']);
    $pass = $conn->real_escape_string($_POST['password']);

    $sql = "SELECT * FROM userinfo WHERE username='$user' AND password='$pass'";
    $result = $conn->query($sql);

    if ($result->num_rows > 0) {
        $row = $result->fetch_assoc();
        
        // Get biometric IDs from database
        $fingerprintID = isset($row['fingerprint']) ? $row['fingerprint'] : 0;
        $faceID = isset($row['face']) ? $row['face'] : 0;

        // Format response for Arduino parsing
        echo "User authenticated!";
        echo "|FingerprintID=" . $fingerprintID;
        echo "|FaceID=" . $faceID;
    } else {
        echo "Authentication failed!";
    }
}

$conn->close();
?>





