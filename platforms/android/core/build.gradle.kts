plugins { `java-library` }
java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}
dependencies {
    implementation("org.yaml:snakeyaml:2.4")
    testImplementation("junit:junit:4.13.2")
}

